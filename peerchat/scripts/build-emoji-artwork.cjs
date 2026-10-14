// Build-only tool: bundled sharp/libvips rasterizes pinned, trusted SVG sources.
// Neither sharp nor an SVG interpreter is shipped in the application.
const fs = require('fs'), path = require('path');
const sharp = require(process.env.PEERCHAT_SHARP || 'sharp');
const [fluent, flags, output] = process.argv.slice(2);
if (!output) throw Error('Usage: fluent-root flag-root output');
const cp = require('child_process');
const fluentPin='1ffb34c752ecf5d402f04cfb4b392c77f57c54bc', flagPin='e20cbc2bbec1926686be9f9bee7d1d2cfa1fea0e';
if(cp.execFileSync('git',['-C',fluent,'rev-parse','HEAD'],{encoding:'utf8'}).trim()!==fluentPin || cp.execFileSync('git',['-C',flags,'rev-parse','HEAD'],{encoding:'utf8'}).trim()!==flagPin)throw Error('Artwork source pin mismatch');
fs.mkdirSync(output, {recursive:true});
const key = text => text.toLowerCase().split(/\s+/).filter(x=>x!=='fe0f').map(x=>parseInt(x,16).toString(16)).join('-');
const tones = ['Default','Light','Medium-Light','Medium','Medium-Dark','Dark'];
const files = [];
for (const name of fs.readdirSync(path.join(fluent,'assets')).sort()) {
 const folder=path.join(fluent,'assets',name), metaFile=path.join(folder,'metadata.json');
 if(!fs.existsSync(metaFile)) continue;
 const m=JSON.parse(fs.readFileSync(metaFile,'utf8'));
 const variants=m.unicodeSkintones || [m.unicode];
 variants.forEach((unicode,i)=>{
  const dir=path.join(folder,m.unicodeSkintones?tones[i]:'','Flat');
  if(!fs.existsSync(dir)) throw Error('Missing Flat variant: '+dir);
  const svg=fs.readdirSync(dir).filter(x=>x.endsWith('.svg'));
  if(svg.length!==1)throw Error('Ambiguous artwork: '+dir);
  files.push({key:key(unicode),source:path.join(dir,svg[0]),origin:'Fluent Flat'});
 });
}
for(const file of fs.readdirSync(flags).sort()) if(/^[A-Z]{2}\.svg$/.test(file)) {
 const country=file.slice(0,2), unicode=Array.from(country).map(c=>(0x1f1e6+c.charCodeAt(0)-65).toString(16)).join(' ');
 files.push({key:key(unicode),source:path.join(flags,file),origin:'Regional flag'});
}
// The newer family pictogram is monochrome. Pin the older Apache-2.0 color SVG.
const familySource=path.resolve(flags,'../../../../noto-family-color-direct/family.svg');
const familySha='d915bc90526f6b6b71607c2dfaa2dda25bb3ecef0b4a3397ec148103d02e37c1';
if(require('crypto').createHash('sha256').update(fs.readFileSync(familySource)).digest('hex')!==familySha)throw Error('Family source hash mismatch');
files.push({key:'1f468-200d-1f469-200d-1f467-200d-1f466',source:familySource,origin:'Noto v2.038 family, e8073ab740292f8d5f19b5de144087ac58044d06 (Apache-2.0)'});
(async()=>{
 const seen=new Set();
 for(const f of files){if(seen.has(f.key))throw Error('Duplicate Unicode identity '+f.key);seen.add(f.key);
  let source=fs.readFileSync(f.source); const alias=source.toString("utf8").trim(); if(f.origin==="Regional flag" && /^[A-Z]{2}\.svg$/.test(alias))source=fs.readFileSync(path.join(flags,alias)); try{await sharp(source,{density:192}).resize(64,64,{fit:"contain",background:{r:0,g:0,b:0,alpha:0}}).png().toFile(path.join(output,f.key+".png"));}catch(e){throw Error(f.source+": "+e.message);}
 }
 fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({format:1,fluentCommit:fluentPin,flagsCommit:flagPin,familySourceSha256:familySha,count:files.length,sharp:sharp.versions,entries:files.map(f=>({key:f.key,origin:f.origin}))},null,2));
 console.log('PASS: '+files.length+' offline 64px color assets; no runtime SVG/network dependency');
})().catch(e=>{console.error(e);process.exitCode=1});
