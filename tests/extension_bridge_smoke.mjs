// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
for (const browserType of ['chromium', 'firefox']) {
  let stored = {key:'0123456789abcdef0123456789abcdef', browser:'Brave', instance:'abcdef0123456789abcdef0123456789'};
  const sends = [];
  const events = () => ({addListener() {}});
  const api = {
    runtime:{getManifest:()=> browserType==='firefox' ? {browser_specific_settings:{gecko:{}}} : {}, onMessage:events()},
    tabs:{query:async()=>[
      {id:1,title:'Test',url:'https://example.org/path',favIconUrl:'https://example.org/favicon.png'},
      {id:2,title:'Secret',url:'https://private.example',incognito:true},
      {id:3,title:'Internal',url:'chrome://settings'}
    ],onCreated:events(),onUpdated:events(),onRemoved:events(),onActivated:events()},
    windows:{onFocusChanged:events()},
    storage:{local:{get:async()=>stored,set:async(v)=>{stored={...stored,...v}}},onChanged:events()},
    alarms:{onAlarm:events(),create() {}}
  };
  const js=fs.readFileSync(new URL(`../extensions/${browserType}/background.js`,import.meta.url),'utf8');
  const context={chrome:api, browser:browserType==='firefox'?api:undefined,
    URL,Uint8Array,crypto:globalThis.crypto,AbortController,setTimeout,clearTimeout,
    fetch:async (url,args)=>{sends.push({url,args});return {ok:true}}};
  vm.runInNewContext(js,context);
  await new Promise(resolve=>setTimeout(resolve,15));
  assert.equal(sends.length,1);
  const body=JSON.parse(sends[0].args.body);
  assert.equal(body.browser,browserType==='firefox'?'Firefox':'Brave');
  assert.equal(body.tabs.length,1);
  assert.equal(body.tabs[0].faviconUrl,'https://example.org/favicon.png');
  assert.equal(sends[0].url,'http://127.0.0.1:48231/v1/tabs');
  assert.equal(sends[0].args.headers['X-ClickNova-Key'],stored.key);
  console.log(`${browserType}: safe extension snapshot OK`);
}
