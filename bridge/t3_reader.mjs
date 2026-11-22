/**
 * Read-only T3 Code SQLite projection reader.
 * Uses Node 24 built-in `node:sqlite` with read-only URI + PRAGMA query_only.
 * Never modifies T3 Code's database.
 */

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";

const ACTIVE_RUN_STATUSES = new Set([
  "running",
  "queued",
  "starting",
  "preparing",
  "waiting",
  "in_progress",
]);

const ACTIVE_ITEM_STATUSES = new Set([
  "running",
  "waiting",
  "queued",
  "in_progress",
  "pending",
  "starting",
]);

export function expandHome(p) {
  if (!p) return p;
  if (p === "~") return os.homedir();
  if (p.startsWith("~/")) return path.join(os.homedir(), p.slice(2));
  return p;
}

function safeJson(str, fallback = null) {
  if (!str || typeof str !== "string") return fallback;
  try {
    return JSON.parse(str);
  } catch {
    return fallback;
  }
}

export function cleanText(str, maxLen = 120) {
  if (!str) return "";
  const s = String(str)
    .replace(/[\r\n\t]+/g, " ")
    .replace(/[\u2014\u2013]/g, "-")
    .replace(/[\u00b7\u2022]/g, "|")
    .replace(/\u2026/g, "...")
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201c\u201d]/g, '"')
    .replace(/[^\x20-\x7E]/g, "")
    .replace(/\s+/g, " ")
    .trim();
  if (s.length <= maxLen) return s;
  return s.slice(0, Math.max(0, maxLen - 3)).trimEnd() + "...";
}

export function formatRelativeTime(isoStr, nowMs = Date.now()) {
  if (!isoStr) return "-";
  const ts = Date.parse(isoStr);
  if (Number.isNaN(ts)) return "-";
  const diffSec = Math.max(0, Math.floor((nowMs - ts) / 1000));
  if (diffSec < 90) return "now";
  const diffMin = Math.floor(diffSec / 60);
  if (diffMin < 60) return `${diffMin}m`;
  const diffHr = Math.floor(diffMin / 60);
  if (diffHr < 48) return `${diffHr}h`;
  return `${Math.floor(diffHr / 24)}d`;
}

export function formatHeaderClock(now = new Date()) {
  const days = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];
  const months = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"];
  const d = days[now.getDay()];
  const dd = String(now.getDate()).padStart(2, "0");
  const mon = months[now.getMonth()];
  const hh = String(now.getHours()).padStart(2, "0");
  const mm = String(now.getMinutes()).padStart(2, "0");
  return `${d} ${dd} ${mon} ${hh}:${mm}`;
}

export function deriveProjectCode(title, overrides = {}, used = new Set()) {
  if (overrides[title]) {
    const code = overrides[title].toUpperCase().slice(0, 2);
    used.add(code);
    return code;
  }

  const raw = String(title || "PR").trim();
  const candidates = [];

  // 1. First letter + first digit if name looks like R6V2Fix / SM80...
  const digitMatch = raw.match(/^([A-Za-z]).*?(\d)/);
  if (digitMatch) {
    candidates.push((digitMatch[1] + digitMatch[2]).toUpperCase());
  }

  // 2. CamelCase / separator initials
  const words = raw
    .replace(/([a-z])([A-Z])/g, "$1 $2")
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean);
  if (words.length >= 2) {
    candidates.push((words[0][0] + words[1][0]).toUpperCase());
  }
  const alnum = raw.replace(/[^A-Za-z0-9]/g, "").toUpperCase();
  if (alnum.length >= 2) {
    candidates.push(alnum.slice(0, 2));
    for (let i = 2; i < alnum.length; i++) {
      candidates.push(alnum[0] + alnum[i]);
    }
  }
  candidates.push("P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9");

  for (const c of candidates) {
    const clean = c.slice(0, 2).padEnd(2, "X");
    if (!used.has(clean)) {
      used.add(clean);
      return clean;
    }
  }
  return "PR";
}

function repoDisplay(workspaceRoot, pullRequests) {
  for (const pr of pullRequests) {
    if (pr && pr.repository) return pr.repository;
  }
  if (workspaceRoot) {
    return path.basename(workspaceRoot);
  }
  return "local";
}

function extractPrsFromPayload(payload) {
  const out = [];
  const seen = new Set();
  const list = [];
  if (payload?.linkedPullRequest) list.push(payload.linkedPullRequest);
  if (Array.isArray(payload?.pullRequests)) list.push(...payload.pullRequests);

  for (const pr of list) {
    if (!pr || typeof pr !== "object" || pr.number == null) continue;
    const key = `${pr.repository || ""}:${pr.number}`;
    if (seen.has(key)) continue;
    seen.add(key);
    const snap = pr.snapshot || pr;
    const rawState = String(snap.state || pr.state || "open").toUpperCase();
    out.push({
      number: Number(pr.number),
      num: `#${pr.number}`,
      state: snap.isDraft && rawState === "OPEN" ? "DRAFT" : rawState,
      title: cleanText(snap.title || pr.title || `PR #${pr.number}`, 88),
      branch: cleanText(snap.headBranch || pr.headBranch || "", 36),
      checksState: snap.checksState || null,
      repository: pr.repository || null,
      updatedAt: snap.updatedAt || pr.linkedAt || null,
    });
  }

  // Open/Draft first, then newest PR number descending
  out.sort((a, b) => {
    const aOpen = a.state === "OPEN" || a.state === "DRAFT" ? 0 : 1;
    const bOpen = b.state === "OPEN" || b.state === "DRAFT" ? 0 : 1;
    if (aOpen !== bOpen) return aOpen - bOpen;
    return b.number - a.number;
  });
  return out;
}

export function readT3Board(config = {}) {
  const dbPath = path.resolve(expandHome(config.t3Database || "~/.t3/userdata/statev2.sqlite"));
  const scratchDir = path.resolve(expandHome(config.scratchDir || "~/.t3/scratch"));
  const overrides = config.projectCodes || {};
  const now = new Date();
  const nowMs = now.getTime();

  if (!fs.existsSync(dbPath)) {
    throw new Error(`T3 database not found at ${dbPath}`);
  }

  const db = new DatabaseSync(dbPath, { readOnly: true });
  try {
    db.exec("PRAGMA query_only = ON");

    // 1. Load live non-scratch projects
    const projectRows = db
      .prepare(
        `SELECT project_id, title, workspace_root
         FROM projection_projects
         WHERE deleted_at IS NULL AND archived_at IS NULL
         ORDER BY created_at ASC`
      )
      .all();

    const projectById = new Map();
    const usedCodes = new Set();

    // First register configured overrides so auto-codes never collide with explicit ones
    for (const row of projectRows) {
      const resolvedRoot = row.workspace_root ? path.resolve(expandHome(row.workspace_root)) : "";
      if (resolvedRoot === scratchDir || row.title === "No project") continue;
      if (overrides[row.title]) {
        usedCodes.add(overrides[row.title].toUpperCase().slice(0, 2));
      }
    }

    for (const row of projectRows) {
      const resolvedRoot = row.workspace_root ? path.resolve(expandHome(row.workspace_root)) : "";
      if (resolvedRoot === scratchDir || row.title === "No project") continue;
      const code = deriveProjectCode(row.title, overrides, usedCodes);
      projectById.set(row.project_id, {
        projectId: row.project_id,
        code,
        name: row.title,
        workspaceRoot: resolvedRoot,
      });
    }

    // 2. Load latest run status per thread
    const latestRunRows = db
      .prepare(
        `SELECT r.thread_id, r.run_id, r.status, r.ordinal, r.requested_at, r.completed_at
         FROM orchestration_v2_projection_runs r
         INNER JOIN (
           SELECT thread_id, MAX(ordinal) AS max_ord
           FROM orchestration_v2_projection_runs
           GROUP BY thread_id
         ) m ON m.thread_id = r.thread_id AND m.max_ord = r.ordinal`
      )
      .all();

    const latestRunByThread = new Map();
    for (const r of latestRunRows) {
      latestRunByThread.set(r.thread_id, r);
    }

    // 3. Load genuinely active / awaited turn items on each thread's latest run
    // (skip todo_list / assistant_message snapshots that can linger in non-terminal states)
    const activeTurnItems = db
      .prepare(
        `SELECT ti.thread_id, ti.run_id, ti.type, ti.status, ti.updated_at, ti.payload_json
         FROM orchestration_v2_projection_turn_items ti
         INNER JOIN (
           SELECT r.thread_id, r.run_id
           FROM orchestration_v2_projection_runs r
           INNER JOIN (
             SELECT thread_id, MAX(ordinal) AS max_ord
             FROM orchestration_v2_projection_runs
             GROUP BY thread_id
           ) m ON m.thread_id = r.thread_id AND m.max_ord = r.ordinal
         ) lr ON lr.thread_id = ti.thread_id AND lr.run_id = ti.run_id
         WHERE ti.status IN ('running', 'waiting', 'queued', 'in_progress', 'pending', 'starting')
           AND ti.type NOT IN ('todo_list', 'assistant_message', 'user_message', 'reasoning', 'plan')
         ORDER BY ti.updated_at DESC`
      )
      .all();

    const activeItemsByThread = new Map();
    for (const item of activeTurnItems) {
      const ip = safeJson(item.payload_json, {}) || {};
      // Only treat turn items as live blockers if explicitly waiting/awaited or an active subagent
      const isLiveBlocker =
        item.status === "waiting" ||
        item.type === "subagent" ||
        ip.awaited === true;
      if (!isLiveBlocker) continue;
      const arr = activeItemsByThread.get(item.thread_id) || [];
      arr.push(item);
      activeItemsByThread.set(item.thread_id, arr);
    }

    // 4. Load non-deleted, non-archived v2 threads belonging to real projects
    const threadRows = db
      .prepare(
        `SELECT thread_id, project_id, title, default_provider, provider_instance_id,
                created_at, updated_at, payload_json
         FROM orchestration_v2_projection_threads
         WHERE deleted_at IS NULL AND archived_at IS NULL
         ORDER BY updated_at DESC`
      )
      .all();

    const parsedThreads = [];
    const threadById = new Map();

    for (const row of threadRows) {
      const proj = projectById.get(row.project_id);
      if (!proj) continue; // Skip "No project" / scratch or deleted projects

      const payload = safeJson(row.payload_json, {}) || {};
      if (payload.archivedAt || payload.deletedAt) continue;

      const isSettled = Boolean(payload.settledAt);
      const latestRun = latestRunByThread.get(row.thread_id);
      const rawRunStatus = String(latestRun?.status || "idle").toLowerCase();
      const pendingItems = isSettled ? [] : (activeItemsByThread.get(row.thread_id) || []);

      let effectiveStatus = rawRunStatus;
      if (!isSettled && !ACTIVE_RUN_STATUSES.has(effectiveStatus) && pendingItems.length > 0) {
        effectiveStatus = "waiting";
      }

      const lineage = payload.lineage || {};
      const parentThreadId = lineage.parentThreadId || null;
      const model =
        payload?.modelSelection?.model ||
        row.default_provider ||
        row.provider_instance_id ||
        "agent";

      const isActive = !isSettled && ACTIVE_RUN_STATUSES.has(effectiveStatus);

      const obj = {
        threadId: row.thread_id,
        projectId: row.project_id,
        project: proj,
        title: cleanText(row.title || payload.title || "Untitled thread", 96),
        model: cleanText(model, 32),
        provider: row.provider_instance_id || row.default_provider || "agent",
        branch: cleanText(payload.branch || "", 40),
        status: isActive ? effectiveStatus : "completed",
        isActive,
        settledAt: payload.settledAt || null,
        updatedAt: row.updated_at || payload.updatedAt || row.created_at,
        parentThreadId,
        relationshipToParent: lineage.relationshipToParent || null,
        prs: extractPrsFromPayload(payload),
        pendingItems,
      };
      parsedThreads.push(obj);
      threadById.set(obj.threadId, obj);
    }

    // Group child threads under parent threads
    const childrenByParent = new Map();
    for (const t of parsedThreads) {
      if (t.parentThreadId) {
        const list = childrenByParent.get(t.parentThreadId) || [];
        list.push(t);
        childrenByParent.set(t.parentThreadId, list);
      }
    }

    // If a parent thread has an active child thread, mark parent as active (waiting) too
    for (const t of parsedThreads) {
      if (t.parentThreadId) continue;
      const kids = childrenByParent.get(t.threadId) || [];
      if (!t.isActive && kids.some((k) => k.isActive)) {
        t.status = "waiting";
        t.isActive = true;
      }
    }

    // 5. Build active project board entries (active top-level project threads only)
    const activeTopThreads = parsedThreads
      .filter((t) => !t.parentThreadId && t.isActive)
      .sort((a, b) => Date.parse(b.updatedAt || 0) - Date.parse(a.updatedAt || 0));

    // Group by project so tapping a project shows all its threads/PRs/subagents cleanly
    const groupedByProject = new Map();
    for (const t of activeTopThreads) {
      let entry = groupedByProject.get(t.projectId);
      if (!entry) {
        entry = {
          project: t.project,
          threads: [],
          prs: [],
          subagents: [],
        };
        groupedByProject.set(t.projectId, entry);
      }
      entry.threads.push(t);
    }

    const projects = [];
    for (const [, group] of groupedByProject) {
      const lead = group.threads[0];
      const allPrs = [];
      const prSeen = new Set();
      for (const th of group.threads) {
        for (const pr of th.prs) {
          if (!prSeen.has(pr.num)) {
            prSeen.add(pr.num);
            allPrs.push(pr);
          }
        }
      }

      // Collect subagents (child threads + non-subagent pending background commands)
      const subagents = [];
      const seenChildIds = new Set();

      for (const th of group.threads) {
        const kids = (childrenByParent.get(th.threadId) || []).slice().sort((a, b) => {
          const aAct = a.isActive ? 0 : 1;
          const bAct = b.isActive ? 0 : 1;
          if (aAct !== bAct) return aAct - bAct;
          return Date.parse(b.updatedAt || 0) - Date.parse(a.updatedAt || 0);
        });

        for (const k of kids) {
          seenChildIds.add(k.threadId);
          const st = k.status === "running" || k.status === "starting" || k.status === "preparing"
            ? "running"
            : k.isActive
            ? "waiting"
            : "done";
          const ago = formatRelativeTime(k.updatedAt, nowMs);
          const meta =
            st === "running"
              ? `RUN ${ago}`
              : st === "waiting"
              ? `WAIT ${ago}`
              : `DONE ${ago}`;
          subagents.push({
            state: st,
            task: cleanText(k.title, 64),
            agent: cleanText(k.model, 24),
            meta,
            updatedAt: k.updatedAt,
          });
        }

        // Also include awaited background commands from pendingItems
        for (const item of th.pendingItems) {
          const ip = safeJson(item.payload_json, {}) || {};
          const childId = ip.childThreadId || ip.delegatedThreadId || null;
          if (childId && seenChildIds.has(childId)) continue;
          if (item.type === "subagent" && childId) continue;
          const desc =
            ip.description ||
            ip.title ||
            ip.command ||
            ip.label ||
            `${item.type} task`;
          subagents.push({
            state: item.status === "running" ? "running" : "waiting",
            task: cleanText(desc, 64),
            agent: item.type === "subagent" ? "subagent" : "bg-cmd",
            meta: item.status === "running" ? "RUNNING" : "AWAITED",
            updatedAt: item.updated_at,
          });
        }
      }

      // Sort subagents: running first, then waiting, then recent done (cap at 6 for 800x600 display)
      const rank = { running: 0, waiting: 1, done: 2 };
      subagents.sort((a, b) => {
        if (rank[a.state] !== rank[b.state]) return rank[a.state] - rank[b.state];
        return Date.parse(b.updatedAt || 0) - Date.parse(a.updatedAt || 0);
      });

      const liveSubCount = subagents.filter((s) => s.state === "running" || s.state === "waiting").length;
      const openPr = allPrs.find((p) => p.state === "OPEN" || p.state === "DRAFT");
      const latestPr = openPr || allPrs[0] || null;
      const liveFirstSub = subagents.find((s) => s.state === "running" || s.state === "waiting");

      const activeWork = openPr
        ? openPr.title
        : liveFirstSub
        ? liveFirstSub.task
        : lead.title;

      const openCount = allPrs.filter((p) => p.state === "OPEN" || p.state === "DRAFT").length;
      const mergedCount = allPrs.filter((p) => p.state === "MERGED").length;
      let prSummary = "";
      if (openCount > 0) {
        prSummary = `${openCount} open` + (mergedCount > 0 ? ` · ${mergedCount} merged` : "");
      } else if (mergedCount > 0) {
        prSummary = `${mergedCount} merged` + (lead.branch ? ` · ${lead.branch}` : "");
      } else if (lead.branch) {
        prSummary = `branch: ${lead.branch}`;
      } else {
        prSummary = `${liveSubCount} live task${liveSubCount === 1 ? "" : "s"}`;
      }

      const displayStatus =
        group.threads.some((t) => t.status === "running") ||
        subagents.some((s) => s.state === "running")
          ? "running"
          : "waiting";

      projects.push({
        code: group.project.code,
        projectId: group.project.projectId,
        name: cleanText(group.project.name, 28),
        repo: cleanText(repoDisplay(group.project.workspaceRoot, allPrs), 44),
        status: displayStatus,
        leadAgent: lead.model,
        leadThread: cleanText(lead.title, 72),
        prBadge: latestPr ? latestPr.num : "",
        activeWork: cleanText(activeWork, 76),
        prSummary: cleanText(prSummary, 44),
        liveSubagents: liveSubCount,
        totalPrs: allPrs.length,
        openPrs: openCount,
        updatedAgo: formatRelativeTime(lead.updatedAt, nowMs),
        subagents: subagents.slice(0, 6),
        prs: allPrs.slice(0, 6),
      });
    }

    const runningProjects = projects.filter((p) => p.status === "running").length;
    const waitingProjects = projects.filter((p) => p.status === "waiting").length;
    const totalLiveSubagents = projects.reduce((s, p) => s + p.liveSubagents, 0);
    const totalOpenPrs = projects.reduce((s, p) => s + p.openPrs, 0);

    let lastSummary = "No active project threads";
    if (projects.length > 0) {
      const p0 = projects[0];
      const topTask = p0.subagents[0]?.task || p0.activeWork;
      lastSummary = cleanText(`${p0.code} · ${topTask}`, 58);
    }

    return {
      generatedAt: now.toISOString(),
      clock: formatHeaderClock(now),
      stats: {
        activeProjects: projects.length,
        running: runningProjects,
        waiting: waitingProjects,
        liveSubagents: totalLiveSubagents,
        openPrs: totalOpenPrs,
      },
      lastSummary,
      projects,
    };
  } finally {
    db.close();
  }
}

/**
 * Compact tab-separated wire format for the zero-dependency C Kindle client.
 * Every field is sanitized of tabs/newlines so parsing in C is trivial and safe.
 */
export function serializeBoardTsv(board) {
  const lines = [];
  const t = (v) => cleanText(v ?? "", 96);
  lines.push(
    [
      "META",
      t(board.clock),
      board.stats.activeProjects,
      board.stats.running,
      board.stats.waiting,
      board.stats.liveSubagents,
      board.stats.openPrs,
      t(board.lastSummary),
    ].join("\t")
  );

  for (const p of board.projects) {
    lines.push(
      [
        "PROJ",
        t(p.code),
        t(p.name),
        t(p.repo),
        t(p.status),
        t(p.leadAgent),
        t(p.leadThread),
        t(p.prBadge),
        t(p.activeWork),
        t(p.prSummary),
        p.liveSubagents,
        p.totalPrs,
        t(p.updatedAgo),
      ].join("\t")
    );
    for (const s of p.subagents) {
      lines.push(["SUB", t(p.code), t(s.state), t(s.task), t(s.agent), t(s.meta)].join("\t"));
    }
    for (const pr of p.prs) {
      lines.push(
        ["PR", t(p.code), t(pr.num), t(pr.state), t(pr.title), t(pr.branch)].join("\t")
      );
    }
  }
  return lines.join("\n") + "\n";
}
