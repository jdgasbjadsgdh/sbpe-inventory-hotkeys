'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict'),path=require('node:path');
const {chromium}=require(process.env.PLAYWRIGHT_PATH || 'playwright');
const here=__dirname,upstream=fs.readFileSync((process.env.WEBBREAK_UPSTREAM || path.join(here,'upstream/VR1574197275.mvmmoclient.js')),'utf8');
const signatures=JSON.parse(fs.readFileSync(path.join(here,'inventory-native-signatures.json'),'utf8'));
const core=require('./inventory-install-core.js'),plugin=fs.readFileSync(path.join(here,'inventory-hotkeys.js'),'utf8');
const payload={plugin,signatures};
const patched=core.update(upstream,payload);
assert(patched.includes('inventory_hotkeys_modify:$ga'));assert.equal(core.update(patched,payload),patched);
const custom=patched.replace('1: "", 2: "", 3: "",','1: "KeyZ", 2: "KeyX", 3: "",');
assert(core.update(custom,payload).includes('1: "KeyZ", 2: "KeyX"'));
assert.throws(()=>core.update(upstream.replace(signatures.functions.modify.source,'bad body'),payload),/signature differs/);
assert.throws(()=>core.update(upstream.replace('return{__GLOBAL__I_000101:P_a','return{other:P_a'),payload),/export table differs/);
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROME_BINARY?{executablePath:process.env.CHROME_BINARY}:{})});
 try{
  const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.setContent('<!doctype html><body><input id="chat"></body>');
  await page.evaluate(()=>{
   const heap=new ArrayBuffer(1<<20);window.heap=heap;
   for(const [n,T] of Object.entries({HEAPU8:Uint8Array,HEAP8:Int8Array,HEAPU16:Uint16Array,HEAP16:Int16Array,HEAPU32:Uint32Array,HEAP32:Int32Array,HEAPF32:Float32Array,HEAPF64:Float64Array}))window[n]=new T(heap);
  });
  await page.addScriptTag({content:upstream.split('WebBreak.symbols=')[0]});
  await page.evaluate(({signatures})=>{
   const WB=WebBreak;window.at=20000;
   const obj=(type)=>{const p=window.at;window.at+=WB.layout[type][0]+64;return WB.mem.cast(type,p);};
   const wc=obj('WorldClient'),cw=obj('ClientWorld'),pc=obj('PlayerCharacter'),conn=obj('ServerConnection'),stage=obj('Stage'),item=obj('Item'),desc=obj('ItemDescription');
   wc.clientWorld=cw;wc.serverConnection=conn;cw.player=pc;cw.mySubWorld=1;cw.id=12;conn.state=3;conn.socket=9;pc.props.hitpoints=100;pc.props.maxhitpoints=100;
   pc.className=()=> 'Wildfire';WB.mem.className=(p)=>p===pc.address?'Wildfire':'';
   pc.numInvSlots=12;pc.numMainSlots=1;item.user=pc;item.desc=desc;item.slotPos=0xf00;desc.slot=0x100;
   // The actual pinned getItem walks this native libc++ hash table.
   const buckets=8000,head=8100,node=8200;HEAP32[(pc.address+652)>>2]=buckets;HEAP32[(pc.address+656)>>2]=1;
   HEAP32[buckets>>2]=head;HEAP32[head>>2]=node;HEAP32[(node+4)>>2]=0xf00;HEAP32[(node+8)>>2]=0xf00;HEAP32[(node+12)>>2]=item.address;
   // Native addModifyInv calls the player virtual moveItem (slot 232).
   HEAP32[pc.address>>2]=8300;HEAP32[(8300+232)>>2]=1;
   window.sent=[];window.moved=[];window.destroyed=0;window.freed=0;
   const bodies=Object.values(signatures.functions).map(x=>x.source).join('\n')+'\n'+signatures.Yh.source;
   const make=new Function('heap','record','move',
    'var a=new Int8Array(heap),c=new Int32Array(heap),d=new Uint8Array(heap),l=9000,tc=[],uc=[];uc[1]=move;function CM(connection,type,p){record(connection,type,p)};'+bodies+
    ';return {inventory_hotkeys_item:ol,inventory_hotkeys_slots:nl,inventory_hotkeys_modify:$ga,inventory_hotkeys_construct:pKa,inventory_hotkeys_destroy:BIa};');
   const asm=make(heap,(connection,type,p)=>sent.push({connection,type,command:HEAP32[(p+4)>>2],from:HEAP32[(p+8)>>2],to:HEAP32[(p+12)>>2],bits:HEAP32[(p+20)>>2]}),(player,from,to)=>{moved.push({player,from,to});return 1;});
   asm._malloc=()=>10000;asm._free=()=>window.freed++;
   const destroy=asm.inventory_hotkeys_destroy;asm.inventory_hotkeys_destroy=p=>{window.destroyed++;destroy(p);};
   WB.asm=asm;WB.sbpe.refs.stage=stage;WB.sbpe.refs.WorldClient=wc;WB.sbpe.refs.ClientWorld=cw;
   WB.sbpe.refs.topTypes=['GameClient'];WB.sbpe.refs.manager={_active:{},reloadPlugin(name){const P=window.InventoryClass;const p=new P(WB.sbpe.refs,name);this._active[name]=p;p.onInit();}};
   // Capture registration while preserving the real PluginBase and config.
   WB.sbpe.register=(name,P)=>{window.InventoryClass=P;const p=new P(WB.sbpe.refs,'plugin_'+name);WB.sbpe.refs.manager._active['plugin_'+name]=p;p.onInit();};
   window.fixture={wc,cw,pc,conn,item,desc,stage};window.update=()=>WB.inventoryHotkeys.plugin.afterUpdate();
  },{signatures});
  await page.addScriptTag({content:plugin.replace('1: "", 2: "", 3: "",','1: "KeyZ", 2: "KeyX", 3: "",')});
  await page.keyboard.down('z');await page.evaluate(()=>{fixture.cw.inputBuffer=150;update();update();});
  assert.deepEqual(await page.evaluate(()=>sent.map(x=>[x.type,x.command,x.from,x.to,x.bits])),[[18,2,0xf00,0x100,7]]);
  assert.equal(await page.evaluate(()=>destroyed),1);assert.equal(await page.evaluate(()=>freed),1);
  await page.keyboard.up('z');
  await page.keyboard.down('z');await page.evaluate(()=>{fixture.cw.inputBuffer=0;update();});await page.keyboard.up('z');assert.equal(await page.evaluate(()=>sent.length),1);
  await page.keyboard.down('x');await page.evaluate(()=>{fixture.cw.inputBuffer=150;update();});await page.keyboard.up('x');assert.equal(await page.evaluate(()=>sent.length),1); // empty slot
  await page.locator('#chat').focus();await page.keyboard.press('z');await page.locator('#chat').blur();await page.evaluate(()=>update());assert.equal(await page.evaluate(()=>sent.length),1);
  await page.keyboard.down('z');await page.evaluate(()=>{fixture.cw.id++;update();});await page.keyboard.up('z');assert.equal(await page.evaluate(()=>sent.length),1);
  await page.evaluate(()=>{fixture.pc.props.hitpoints=0;});await page.keyboard.press('z');await page.evaluate(()=>update());assert.equal(await page.evaluate(()=>sent.length),1);
  await page.evaluate(()=>{fixture.pc.props.hitpoints=100;WebBreak.sbpe.refs.manager._active.plugin_sc_boss_test={mode:'holding'};});await page.keyboard.press('z');await page.evaluate(()=>update());assert.equal(await page.evaluate(()=>sent.length),1);
  await page.evaluate(()=>{delete WebBreak.sbpe.refs.manager._active.plugin_sc_boss_test;});await page.keyboard.press('Control+z');await page.evaluate(()=>update());assert.equal(await page.evaluate(()=>sent.length),1);
  await page.keyboard.down('z');await page.evaluate(()=>{fixture.cw.inputBuffer=150;update();});await page.keyboard.up('z');assert.equal(await page.evaluate(()=>sent.length),2);
  assert.deepEqual(errors,[]);console.log('PASS: real WebBreak ABI/DOM, pinned native getItem/getNumPos/ModifyInv ctor/dtor/handleModifyInv/addModifyInv, accepted swap and budget rejection, keys/repeats/chat/modifiers/room transition/death/hold guards, installer idempotence/key preservation/signature rejection.');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
