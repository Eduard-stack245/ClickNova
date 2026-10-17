const api = typeof browser !== "undefined" ? browser : chrome;
// SPDX-License-Identifier: Apache-2.0
const keyInput = document.getElementById("key");
const browserInput = document.getElementById("browser");
const status = document.getElementById("status");
void api.storage.local.get(["key", "browser"]).then(data => {
  keyInput.value = data.key || "";
  browserInput.value = api.runtime.getManifest().browser_specific_settings ? "Firefox" : (data.browser || "Chromium");
  if (api.runtime.getManifest().browser_specific_settings) browserInput.disabled = true;
});
document.getElementById("save").addEventListener("click", async () => {
  const key = keyInput.value.trim().toLowerCase();
  if (!/^[0-9a-f]{32}$/.test(key)) { status.textContent = "Key must contain 32 hex characters / Код должен содержать 32 символа"; return; }
  await api.storage.local.set({key, browser: browserInput.value});
  try {
    const result = api.runtime.sendMessage({type: "clicknova-sync"});
    if (result && typeof result.catch === "function") result.catch(() => {});
  } catch (_) { /* Background will retry on the next alarm or tab update. */ }
  status.textContent = "Paired. Open ClickNova → Find element → Refresh targets. / Подключено. Обновите список в ClickNova.";
});
document.getElementById("clear").addEventListener("click", async () => {
  await api.storage.local.remove("key");
  keyInput.value = "";
  status.textContent = "Disconnected / Отключено";
});
