import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const names=['sync','accsaber_notplayed','accsaber_toimprove','accsaber_ppgain','accsaber_clan'];
const copies=JSON.parse(fs.readFileSync('mod.template.json','utf8')).fileCopies;
const pcvr=fs.readFileSync('pcvr/package.ps1','utf8');
const quest=fs.readFileSync('scripts/createqmod.ps1','utf8');
for(const name of names){
  const root=fs.readFileSync(name+'.png'), mirror=fs.readFileSync('assets/'+name+'.png');
  assert(root.equals(mirror),name+' mirror differs');
  assert(root.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])),name+' is not PNG');
  assert(root.readUInt32BE(16)===512 && root.readUInt32BE(20)===512,name+' dimensions');
  const packaged=copies.filter(item=>item.name===name+'.png');
  if(name==='accsaber_clan')assert(packaged.length===0,'reserved clan image must not ship yet');
  else {
    assert(packaged.length===1 && packaged[0].destination.endsWith('/'+name+'.png'),name+' Quest mapping');
    assert(quest.includes('"'+name+'.png"'),name+' Quest archive list');
    assert(pcvr.includes("'"+name+"'"),name+' PCVR mapping');
  }
  console.log(name+': '+crypto.createHash('sha256').update(root).digest('hex'));
}
console.log('5 supplied artwork mirrors, PNG dimensions and package definitions passed.');
