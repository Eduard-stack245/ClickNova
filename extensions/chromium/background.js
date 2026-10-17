// SPDX-License-Identifier: Apache-2.0
// Local inventory only: does not access page DOM, cookies or browsing history.
// Explicit opt-in: without a stored pairing key, nothing is transmitted.
const api = typeof browser !== "undefined" ? browser : chrome;
const endpoint = "http://127.0.0.1:48231/v1/tabs";
const isFirefox = !!api.runtime.getManifest().browser_specific_settings;
let pending = false;

function newInstance() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("");
}
function validKey(key) { return typeof key === "string" && /^[0-9a-f]{32}$/.test(key); }
function safeWebTab(tab) {
  if (!tab || tab.incognito || typeof tab.url !== "string") return null;
  try {
    const url = new URL(tab.url);
    if (!(["http:", "https:"].includes(url.protocol)) || !url.hostname) return null;
    const fav = typeof tab.favIconUrl === "string" ? tab.favIconUrl : "";
    return {
      id: String(tab.id),
      title: String(tab.title || url.hostname).slice(0, 512),
      url: url.href.slice(0, 4096),
      // browser-provided icon metadata, not guessed from the tab title
      faviconUrl: fav.slice(0, 4096)
    };
  } catch (_) { return null; }
}
function browserLabel(settings) {
  if (isFirefox) return "Firefox";
  return ["Chromium", "Chrome", "Edge", "Brave", "Opera", "Vivaldi"].includes(settings.browser)
    ? settings.browser : "Chromium";
}
async function syncTabs() {
  if (pending) return;
  pending = true;
  try {
    const settings = await api.storage.local.get(["key", "instance", "browser"]);
    if (!validKey(settings.key)) return;
    let instance = settings.instance;
    if (!/^[0-9a-f]{32}$/.test(instance || "")) {
      instance = newInstance();
      await api.storage.local.set({instance});
    }
    const open = await api.tabs.query({});
    const tabs = open.map(safeWebTab).filter(Boolean).slice(0, 250);
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2500);
    try {
      await fetch(endpoint, {
        method: "POST",
        headers: {"Content-Type": "application/json", "X-ClickNova-Key": settings.key},
        body: JSON.stringify({version: 1, instance, browser: browserLabel(settings), tabs}),
        cache: "no-store",
        signal: controller.signal
      });
    } finally { clearTimeout(timeout); }
  } catch (_) {
    // ClickNova may be closed; no tab data is queued on disk or sent elsewhere.
  } finally { pending = false; }
}
api.runtime.onMessage.addListener(message => {
  if (message && message.type === "clicknova-sync") void syncTabs();
});
api.tabs.onCreated.addListener(() => void syncTabs());
api.tabs.onUpdated.addListener(() => void syncTabs());
api.tabs.onRemoved.addListener(() => void syncTabs());
api.tabs.onActivated.addListener(() => void syncTabs());
api.windows.onFocusChanged.addListener(() => void syncTabs());
api.storage.onChanged.addListener(() => void syncTabs());
api.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === "clicknova-inventory") void syncTabs();
});
api.alarms.create("clicknova-inventory", {periodInMinutes: 0.5});
void syncTabs();
