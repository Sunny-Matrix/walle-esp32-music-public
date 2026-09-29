const savedBaseUrl = localStorage.getItem('waliBaseUrl');
// The ESP32 does not advertise wali.local on every router. Migrate the old
// hostname to the device address found on the current LAN.
const state = { baseUrl: savedBaseUrl && savedBaseUrl !== 'http://wali.local' ? savedBaseUrl : 'http://192.168.1.100', connected: false, left: 0, right: 0, lightsOn: false, color: '#F26A2E', brightness: 80, lastSend: 0, pending: null, joystickActive: false };
const $ = (id) => document.getElementById(id);
const toast = (message) => { const el = $('toast'); el.textContent = message; el.classList.add('show'); clearTimeout(toast.timer); toast.timer = setTimeout(() => el.classList.remove('show'), 1800); };
// Send a simple text/plain POST directly to the ESP32. This avoids CORS
// preflight and removes the extra proxy hop that adds noticeable latency.
const api = (path, body) => { const started = performance.now(); return fetch(`${state.baseUrl}${path}`, { method: body ? 'POST' : 'GET', body: body ? JSON.stringify(body) : undefined }).then((response) => { $('latencyText').textContent = `${Math.round(performance.now()-started)} ms`; if (!response.ok) throw new Error('设备返回错误'); return response.json().catch(() => ({})); }); };
function setConnected(connected) { state.connected = connected; $('statusDot').classList.toggle('online', connected); $('statusText').textContent = connected ? '已连接' : '未连接'; $('connectButton').textContent = connected ? '刷新状态' : '连接设备'; }
async function connect() { const started = performance.now(); try { const data = await api('/api/status'); setConnected(data.online !== false); $('connectionTime').textContent = Math.round(performance.now()-started); if (data.lightsOn !== undefined) { state.lightsOn = data.lightsOn; $('lightsToggle').checked = state.lightsOn; updateLightVisual(); } toast('设备连接成功'); } catch (error) { setConnected(false); toast('无法连接设备'); } }
function sendDrive(left, right, immediate=false) { state.left = Math.round(Math.max(-100,Math.min(100,left))); state.right = Math.round(Math.max(-100,Math.min(100,right))); $('leftMotorValue').textContent = state.left; $('rightMotorValue').textContent = state.right; const now = performance.now(); const run = () => { state.lastSend = performance.now(); api(`/api/control/drive?left=${state.left}&right=${state.right}`).then(()=>setConnected(true)).catch(()=>toast('控制请求失败')); }; if (immediate || now-state.lastSend > 80) run(); else { clearTimeout(state.pending); state.pending = setTimeout(run,80); } }
function stopDrive() { clearTimeout(state.pending); sendDrive(0,0,true); }
function command(name) { const speed = 55; if(name==='forward')sendDrive(speed,speed,true); else if(name==='back')sendDrive(-speed,-speed,true); else if(name==='left')sendDrive(-speed,speed,true); else if(name==='right')sendDrive(speed,-speed,true); else stopDrive(); }
function updateJoystick(event) { const box=$('joystick').getBoundingClientRect(); const max=box.width*.39; const x=Math.max(-1,Math.min(1,(event.clientX-(box.left+box.width/2))/max)); const y=Math.max(-1,Math.min(1,(box.top+box.height/2-event.clientY)/max)); $('joystickKnob').style.transform=`translate(calc(-50% + ${x*max}px),calc(-50% - ${y*max}px))`; $('joystickX').textContent=`X ${Math.round(x*100)}`; $('joystickY').textContent=`Y ${Math.round(y*100)}`; const turn=x*70, forward=y*70; sendDrive(forward+turn,forward-turn); }
function resetJoystick(){state.joystickActive=false; $('joystickKnob').style.transform='translate(-50%,-50%)'; $('joystickX').textContent='X 0'; $('joystickY').textContent='Y 0'; stopDrive();}
function updateLightVisual(){ $('lightState').textContent=state.lightsOn?'灯光开启':'灯光关闭'; $('eyePreview').style.setProperty('--accent',state.color); $('eyePreview').style.opacity=state.lightsOn?1:.42; }
function sendLights(){ const hex=state.color.slice(1); const rgb=[parseInt(hex.slice(0,2),16),parseInt(hex.slice(2,4),16),parseInt(hex.slice(4,6),16)].map(v=>Math.round(v*state.brightness/100)); api('/api/control/lights',{on:state.lightsOn,r:rgb[0],g:rgb[1],b:rgb[2]}).catch(()=>setConnected(false)); }
document.querySelectorAll('.tab').forEach(tab=>tab.addEventListener('click',()=>{document.querySelectorAll('.tab,.page').forEach(el=>el.classList.remove('active'));tab.classList.add('active');$(tab.dataset.tab).classList.add('active');}));
document.querySelectorAll('[data-command]').forEach(button=>{const press=(event)=>{event.preventDefault();command(button.dataset.command)};button.addEventListener('pointerdown',press);button.addEventListener('pointerup',stopDrive);button.addEventListener('pointercancel',stopDrive);button.addEventListener('pointerleave',(event)=>{if(event.buttons)stopDrive();});});
$('joystick').addEventListener('pointerdown',(event)=>{state.joystickActive=true;$('joystick').setPointerCapture(event.pointerId);updateJoystick(event)});$('joystick').addEventListener('pointermove',(event)=>{if(state.joystickActive)updateJoystick(event)});$('joystick').addEventListener('pointerup',resetJoystick);$('joystick').addEventListener('pointercancel',resetJoystick);
const servoRequests = {};
function queueServo(name, value, immediate = false) {
  const request = servoRequests[name] || (servoRequests[name] = {timer: null, busy: false, value});
  request.value = value;
  clearTimeout(request.timer);
  const send = async () => {
    if (request.busy) return;
    request.busy = true;
    const sendingValue = request.value;
    try {
      await api(`/api/control/servo?name=${encodeURIComponent(name)}&value=${sendingValue}`).then(() => setConnected(true));
    } catch (_) {
      toast('舵机请求失败');
    } finally {
      request.busy = false;
      if (request.value !== sendingValue) request.timer = setTimeout(send, 20);
    }
  };
  request.timer = setTimeout(send, immediate ? 0 : 70);
}
document.querySelectorAll('[data-servo]').forEach(input=>{
  input.addEventListener('input',()=>{const name=input.dataset.servo;$(name+'Value').textContent=`${input.value}°`;queueServo(name,Number(input.value));});
  input.addEventListener('change',()=>queueServo(input.dataset.servo,Number(input.value),true));
});
$('lightsToggle').addEventListener('change',()=>{state.lightsOn=$('lightsToggle').checked;updateLightVisual();sendLights()});$('lightColor').addEventListener('input',()=>{state.color=$('lightColor').value.toUpperCase();$('colorValue').textContent=state.color;updateLightVisual();if(state.lightsOn)sendLights()});document.querySelectorAll('[data-color]').forEach(btn=>btn.addEventListener('click',()=>{$('lightColor').value=btn.dataset.color;state.color=btn.dataset.color; $('colorValue').textContent=state.color;if(state.lightsOn)sendLights();updateLightVisual()}));$('brightness').addEventListener('input',()=>{$('brightnessValue').textContent=`${$('brightness').value}%`;state.brightness=Number($('brightness').value);if(state.lightsOn)sendLights()});
async function setServo(name, value) { return api(`/api/control/servo?name=${encodeURIComponent(name)}&value=${value}`).then(() => setConnected(true)); }
async function pose(name) {
  if (name === 'welcome') {
    await setServo('leftArm', 145);
    await setServo('rightArm', 35);
    setTimeout(() => setServo('leftArm', 90), 700);
    setTimeout(() => setServo('rightArm', 90), 1100);
  } else if (name === 'nod') {
    await setServo('head', 62);
    setTimeout(() => setServo('head', 118), 350);
    setTimeout(() => setServo('head', 90), 700);
  } else if (name === 'dance') {
    await setServo('leftArm', 145);
    await setServo('rightArm', 35);
    await setServo('head', 65);
    setTimeout(async () => { await setServo('leftArm', 35); await setServo('rightArm', 145); await setServo('head', 115); }, 700);
    setTimeout(async () => { await setServo('leftArm', 90); await setServo('rightArm', 90); await setServo('head', 90); }, 1500);
  } else if (name === 'reset') {
    await Promise.all(['leftArm', 'rightArm', 'head'].map((servo) => setServo(servo, 90)));
  }
}
document.querySelectorAll('[data-pose]').forEach(btn=>btn.addEventListener('click',()=>{toast(`${btn.querySelector('strong').textContent}动作已发送`);pose(btn.dataset.pose).catch(()=>toast('动作请求失败'));}));$('connectButton').addEventListener('click',connect);$('saveUrl').addEventListener('click',()=>{state.baseUrl=$('deviceUrl').value.replace(/\/$/,'');localStorage.setItem('waliBaseUrl',state.baseUrl);connect()});$('deviceUrl').value=state.baseUrl;$('themeButton').addEventListener('click',()=>document.body.classList.toggle('dark'));$('settingsButton').addEventListener('click',()=>document.querySelector('[data-tab="settings"]').click());
updateLightVisual();connect();
