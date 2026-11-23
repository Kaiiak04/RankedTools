// Synthetic contracts based on AccSaber backend 7a97d2424a6f89d48245335645e86c1ee65966bf.
// Deliberately not represented as live API captures.
import fs from 'node:fs';
const out = new URL('./fixtures/accsaber/', import.meta.url);
fs.mkdirSync(out, {recursive:true});
const write = (name, data) => fs.writeFileSync(new URL(name+'.json', out), JSON.stringify(data,null,2)+'\n');
const scoreCurve = {id:'score-curve',type:'POINT_LOOKUP',scale:61,shift:-18,
  points:[{x:0,y:0},{x:.9,y:.2},{x:.95,y:.5},{x:.98,y:1},{x:1,y:2}]};
const weightCurve = {id:'weight-curve',type:'FORMULA',formula:'LOGISTIC_SIGMOID',xParameterValue:.4,yParameterValue:.1,zParameterValue:15};
write('categories', ['true_acc','standard_acc','tech_acc'].map((code,i)=>({id:'category-'+i,code,
  countForOverall:true,scoreCurve,weightCurve})));
write('modifiers',[{id:'mod-nf',code:'NF',name:'No Fail',multiplier:.5},{id:'mod-fs',code:'FS',name:'Faster Song',multiplier:1}]);
write('profile',{id:'76561198000000001',blId:'123',ssId:'456',name:'Acc Player',avatarUrl:'',country:'US'});
const hash = i => i.toString(16).padStart(40,'0');
const chart = (id,category,complexity,characteristic='Standard') => ({id:'chart-'+id,mapId:'map-'+id,
  difficulty:'EXPERT_PLUS',characteristic,status:'RANKED',categoryId:'category-'+category,complexity,maxScore:100000});
const maps = [1,2,3,4,5].map(i=>({id:'map-'+i,songHash:hash(i),songName:'Acc song '+i,
  songAuthor:'Author',mapAuthor:'Mapper',difficulties:[chart(i,i===3?2:i===4?1:0,i===3?10:i===4?0:7)]}));
maps[3].difficulties.push(chart(40,1,0,'OneSaber'));
const page = (content,number,totalElements,totalPages,size=3) => ({content,page:{number,size,totalElements,totalPages}});
write('maps-0',page(maps.slice(0,3),0,5,2)); write('maps-1',page(maps.slice(3),1,5,2));
const score = (i,accuracy,ap,noMods,mods=[]) => ({id:'score-'+i,userId:'76561198000000001',songHash:hash(i),
  mapDifficultyId:'chart-'+i,songName:'Acc song '+i,difficulty:'EXPERT_PLUS',characteristic:'Standard',
  score:Math.floor(accuracy*100000),scoreNoMods:noMods,accuracy,ap,active:true,partial:false,
  timeSet:'2026-11-01T12:00:00Z',modifierIds:mods});
write('scores-0',page([score(1,.95,762.5,95000),score(4,.98,1098,98000),score(2,.475,160.972222,95000,['mod-nf'])],0,3,1));
write('account-search',{content:[{userId:'76561198000000001',userName:'Acc Player',avatarUrl:'',country:'US',ranking:2}],
  page:{number:0,size:5,totalElements:1,totalPages:1}});
