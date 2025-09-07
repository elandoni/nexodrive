const tbody = document.getElementById('tbody');
const form   = document.getElementById('upForm');
const fileIn = document.getElementById('f');
const upBtn  = document.getElementById('up');
const msg    = document.getElementById('msg');
const upUI   = document.getElementById('upState');
upUI.hidden = true; // oculto inicialmente 
const bar    = document.getElementById('progBar');
const pct    = document.getElementById('progPct');
const upMsg  = document.getElementById('upMsg');
const uploaderIn = document.getElementById('uploader');

const fmtBytes = n => n<1024? n+' B' : n<1048576? (n/1024).toFixed(1)+' KB' : (n/1048576).toFixed(1)+' MB';
const fmtDate  = ts => !ts? '—' : new Date(Number(ts)).toLocaleString();

async function loadList(){
  const r = await fetch('/api/list');
  const j = await r.json();
  tbody.innerHTML = '';
  j.files.forEach(f=>{
    const tr=document.createElement('tr');
    const tdN=document.createElement('td'); tdN.textContent=f.name.replace('/u/','');
    const tdS=document.createElement('td'); tdS.textContent=fmtBytes(f.size);
    const tdD=document.createElement('td'); tdD.textContent=fmtDate(f.ts);
    const tdU=document.createElement('td'); tdU.textContent=f.uploader || '—';
    const tdA=document.createElement('td');
    const a=document.createElement('a');
    a.href='/api/download?path='+ encodeURIComponent(f.name);
    a.textContent='Descargar';
    tdA.appendChild(a);
    tr.append(tdN,tdS,tdD,tdU,tdA); tbody.appendChild(tr);
  });
}

/* Subida con progreso */
let uploading=false;
function setUploading(on){
  uploading = on;
  upBtn.disabled = on;
  fileIn.disabled = on;
  uploaderIn.disabled = on;
  upUI.hidden = !on;
  upBtn.textContent = on ? 'Subiendo…' : 'Subir';
}
function resetProgress(){
  bar.value = 0; pct.textContent = '0%';
  upMsg.textContent = 'Subiendo… no cierres esta página.';
}
function resetUI(){
    setUploading(false);
    resetProgress();
}

window.addEventListener('beforeunload', (e)=>{
  if(uploading){ e.preventDefault(); e.returnValue=''; }
});

form.addEventListener('submit', (e)=>{
  e.preventDefault();
  msg.textContent='';
  if(!fileIn.files.length) return;

  const f = fileIn.files[0];
  const fd = new FormData();
  fd.append('file', f, f.name);
  fd.append('name', f.name);
  fd.append('uploader', uploaderIn.value || '');
  fd.append('ts', Date.now().toString());

  const xhr = new XMLHttpRequest();
  xhr.open('POST','/api/upload');

  setUploading(true); resetProgress();

  xhr.upload.onprogress = (ev)=>{
    if(ev.lengthComputable){
      const p = Math.round(ev.loaded*100/ev.total);
      bar.value = p; pct.textContent = p+'%';
    }
  };

xhr.onload = async ()=>{
  if(xhr.status===200){
    try{
      const r = JSON.parse(xhr.responseText||'{}');
      if(r.ok){ msg.textContent='OK'; await loadList(); form.reset(); return; }
    }catch(_){}
    msg.textContent='Error de respuesta';
  }else{
    msg.textContent='Error HTTP '+xhr.status;
  }
};

xhr.onerror = ()=>{
  msg.textContent='Fallo de red';
};

xhr.onloadend = ()=>{
  resetUI(); // oculta spinner y limpia barra SIEMPRE
};


  xhr.send(fd);
});

loadList();
