/* Inventory installer: preserve existing WebBreak settings and other plugins. */
(function(root,factory){
  var api=factory(); if(typeof module==='object'&&module.exports)module.exports=api;else root.SBPEInventoryInstaller=api;
})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  var fileName='VR1574197275.mvmmoclient.js';
  var marker='/* WebBreak runtime                                                           */';
  var begin='/* SBPE INVENTORY HOTKEYS — WebBreak build 1574197275 */',end='/* END SBPE INVENTORY HOTKEYS */';
  var exportPoint='return{__GLOBAL__I_000101:P_a';
  var exports='inventory_hotkeys_item:ol,inventory_hotkeys_slots:nl,inventory_hotkeys_modify:$ga,inventory_hotkeys_construct:pKa,inventory_hotkeys_destroy:BIa,';
  function update(text,payload){
    if(!text.startsWith('/*@WebBreak-patched*/')||text.split(marker).length!==2)throw new Error('Choose the WebBreak build 1574197275 override. No files changed.');
    Object.keys(payload.signatures.functions).forEach(function(role){
      var body=payload.signatures.functions[role].source;
      if(text.split(body).length!==2)throw new Error('Native inventory signature differs: '+role+'. No files changed.');
    });
    for(var check of ['Yh','T2'])if(text.split(payload.signatures[check].source).length!==2)throw new Error('Native inventory UI/acceptance differs. No files changed.');
    var plugin=payload.plugin.trimEnd(),first=text.indexOf(begin);
    if(first>=0){
      if(text.indexOf(begin,first+begin.length)>=0)throw new Error('Duplicate inventory plugins. No files changed.');
      var last=text.indexOf(end,first);if(last<0||last>text.indexOf(marker))throw new Error('Unrecognized inventory plugin. No files changed.');
      var old=text.slice(first,last),keys=/var SLOT_KEYS = \{[\s\S]*?\};/.exec(old);
      if(!keys)throw new Error('Cannot preserve existing inventory keys. No files changed.');
      plugin=plugin.replace(/var SLOT_KEYS = \{[\s\S]*?\};/,keys[0]);
      text=text.slice(0,first)+plugin+text.slice(last+end.length);
    }else text=text.replace(marker,plugin+'\n\n'+marker);
    if(!text.includes('inventory_hotkeys_modify:$ga')){
      if(text.split(exportPoint).length!==2)throw new Error('Native export table differs. No files changed.');
      text=text.replace(exportPoint,'return{'+exports+'__GLOBAL__I_000101:P_a');
    }
    return text;
  }
  async function install(directory,payload){
    var domain=directory.name==='www.starbreak.com'?directory:await directory.getDirectoryHandle('www.starbreak.com',{create:true});
    var existing=null;try{existing=await domain.getFileHandle(fileName);}catch(e){if(e.name!=='NotFoundError')throw e;}
    var previous=existing?await(await existing.getFile()).text():null;
    var next=update(previous===null?payload.upstream:previous,payload);
    if(next===previous)return{changed:false,file:fileName};
    var backup=null;
    if(previous!==null){backup=fileName+'.inventory-backup-'+new Date().toISOString().replace(/[:.]/g,'-');var saved=await(await domain.getFileHandle(backup,{create:true})).createWritable();await saved.write(previous);await saved.close();}
    var target=existing||await domain.getFileHandle(fileName,{create:true}),writer=await target.createWritable();
    try{await writer.write(next);await writer.close();}catch(e){await writer.abort().catch(function(){});throw e;}
    return{changed:true,file:fileName,backup:backup};
  }
  return{update:update,install:install};
});
