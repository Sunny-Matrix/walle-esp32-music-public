const storageKey = 'waliLearningRecords';
const deviceStorageKey = 'waliLearningDeviceUrl';
const demoMode = new URLSearchParams(window.location.search).get('demo') === '1';
const demoRecords = [{
  subject:'数学',
  topic:'鸡兔同笼：35个头、94条腿',
  duration:8,
  outcome:'已完成',
  note:'通过“假设全是鸡”找到24条腿的差值，理解每换成1只兔子会多2条腿，求出兔12只、鸡23只并完成验算。',
  createdAt:new Date().toISOString(),
  sourceEventId:'demo-chicken-rabbit',
  demoType:'chicken-rabbit'
}];
const state = { records:loadRecords(), deviceUrl:localStorage.getItem(deviceStorageKey) || 'http://192.168.1.100', lastEventId:0, syncInitialized:false, syncing:false };
const $ = (id) => document.getElementById(id);
function loadRecords() { if (demoMode) return demoRecords; try { return JSON.parse(localStorage.getItem(storageKey) || '[]'); } catch (_) { return []; } }
function saveRecords() { if (!demoMode) localStorage.setItem(storageKey, JSON.stringify(state.records)); }
function toast(message) { const el = $('toast'); el.textContent = message; el.classList.add('show'); clearTimeout(toast.timer); toast.timer = setTimeout(() => el.classList.remove('show'), 1800); }
function formatDate(value) { return new Intl.DateTimeFormat('zh-CN', { month:'numeric', day:'numeric' }).format(new Date(value)); }
function iconFor(subject) { return ({数学:'数',科学:'科',语文:'文',英语:'英','综合实践':'综'})[subject] || '学'; }
function setInsight(id, title, text) { $(id + 'Title').textContent = title; $(id + 'Text').textContent = text; }
function renderInsights(records) {
  if (!records.length) {
    setInsight('progress', '等待学习记录', '完成一次问答后，这里会归纳学生主动探索的主题和学习投入。');
    setInsight('focus', '暂无待巩固主题', '系统会根据“进行中”和“需要巩固”的记录提示后续关注方向。');
    setInsight('advice', '从一个小问题开始', '建议学生用自己的话复述一次关键线索，再尝试解决相似问题。');
    return;
  }
  if (records[0]?.demoType === 'chicken-rabbit') {
    setInsight('progress', '能用假设法拆解数量关系', '学生能从35个头确定总只数，以“全部是鸡”的70条腿为基准，发现与94条腿相差24条；进一步理解每把1只鸡换成兔子会多2条腿，最终求出兔12只、鸡23只，并用头数和腿数完成验算。');
    setInsight('focus', '独立解释关键一步仍需加强', '学生已经能够跟随“24÷2=12”的推理，但还需要独立说明为什么要除以2，并尝试把口头思路写成“鸡数+兔数=35、鸡腿+兔腿=94”的数量关系，避免只记住计算步骤。');
    setInsight('advice', '用变式题检验方法迁移', '下一次可更换头数和腿数，让学生先独立说出“全鸡基准、实际腿差、每只腿差”三个关键量，再用方程或代入法复核结果。如果能解释每一步的理由，说明已经真正掌握，而不只是记住答案。');
    return;
  }
  const recentRecords = records.slice(0, 6);
  const subjects = [...new Set(recentRecords.map((record) => record.subject))];
  const completed = recentRecords.filter((record) => record.outcome === '已完成');
  const needsFocus = recentRecords.filter((record) => record.outcome !== '已完成');
  const focusTopics = [...new Set(needsFocus.map((record) => record.topic))].slice(0, 2);
  const recentTopics = [...new Set(recentRecords.map((record) => record.topic))].slice(0, 2);

  setInsight(
    'progress',
    recentRecords.length === 1 ? '完成第一次主动探索' : `近期完成 ${recentRecords.length} 次问答`,
    `涉及${subjects.join('、')}，最近关注“${recentTopics.join('”“')}”。`
  );
  if (needsFocus.length) {
    setInsight(
      'focus',
      `${needsFocus.length} 个主题需要跟进`,
      `建议先回顾“${focusTopics.join('”“')}”中的关键线索。`
    );
  } else {
    setInsight('focus', '本轮主题已完成', '当前记录均标记为已完成。后续可通过相似题或生活情境检验是否真正理解。');
  }
  if (completed.length) {
    setInsight('advice', '尝试迁移到相似问题', '请学生用自己的话复述解题或探究步骤，再完成一道同类型小题，巩固方法而不是记住答案。');
  } else {
    setInsight('advice', '把任务再拆小一点', '下次从“题目已知什么”或“我观察到了什么”开始回答，完成一个小步骤后再继续。');
  }
}
function render() {
  const records = state.records;
  $('sessionCount').textContent = records.length;
  $('minutesTotal').textContent = records.reduce((sum, record) => sum + Number(record.duration || 0), 0);
  $('topicCount').textContent = new Set(records.map((record) => record.topic)).size;
  $('latestSubject').textContent = records[0]?.subject || '--';
  $('latestDate').textContent = records[0] ? formatDate(records[0].createdAt) : '暂无记录';
  $('recordList').innerHTML = records.map((record) => `<article class="record-item"><span class="record-icon">${iconFor(record.subject)}</span><div class="record-main"><strong>${escapeHtml(record.topic)}</strong><small>${escapeHtml(record.subject)} · ${escapeHtml(record.note || '已记录一次学习')}</small></div><div class="record-meta"><strong>${escapeHtml(record.outcome)}</strong><small>${record.duration} 分钟 · ${formatDate(record.createdAt)}</small></div></article>`).join('');
  $('emptyState').hidden = records.length > 0;
  renderInsights(records);
}
function escapeHtml(value) { return String(value).replace(/[&<>'"]/g, (char) => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char])); }
$('clearRecords').addEventListener('click', () => { if (!state.records.length) return; if (window.confirm('确定清空当前浏览器中的学习记录吗？')) { state.records = []; saveRecords(); render(); toast('记录已清空'); } });
$('themeButton').addEventListener('click', () => document.body.classList.toggle('dark'));
function inferSubject(question) { if (/[加减乘除数字算式几何面积周长]/.test(question)) return '数学'; if (/[雨云水蒸发科学实验为什么自然]/.test(question)) return '科学'; if (/[英语单词句子English]/i.test(question)) return '英语'; if (/[阅读作文古诗词语文]/.test(question)) return '语文'; return '综合实践'; }
function shortenTopic(question) { const value = question.replace(/[。！？!?]+$/, '').trim(); return value.length > 28 ? `${value.slice(0, 28)}…` : value; }
function updateLive(events) {
  const lastUser = [...events].reverse().find((event) => event.role === 'user');
  const lastAssistant = [...events].reverse().find((event) => event.role === 'assistant');
  if (!lastUser && !lastAssistant) return;
  $('livePanel').hidden = false;
  if (lastUser) $('liveQuestion').textContent = lastUser.content;
  if (lastAssistant) $('liveAnswer').textContent = lastAssistant.content;
}
function addSyncedRecord(question, answer, eventId) {
  if (!question || !answer || state.records.some((record) => record.sourceEventId === eventId)) return;
  state.records.unshift({ subject:inferSubject(question), topic:shortenTopic(question), duration:10, outcome:'进行中', note:answer.slice(0, 120), createdAt:new Date().toISOString(), sourceEventId:eventId });
  saveRecords(); render(); toast('已同步瓦力本次问答');
}
async function syncLearning() {
  if (state.syncing || !state.deviceUrl) return;
  state.syncing = true;
  try {
    const response = await fetch(`${state.deviceUrl.replace(/\/$/, '')}/api/learning/recent`, { cache:'no-store' });
    if (!response.ok) throw new Error('设备接口不可用');
    const data = await response.json();
    const events = Array.isArray(data.events) ? data.events : [];
    updateLive(events);
    if (!state.syncInitialized) {
      const latestAnswer = [...events].reverse().find((event) => event.role === 'assistant');
      if (latestAnswer) {
        const latestQuestion = [...events].reverse().find((event) => event.role === 'user' && Number(event.id) < Number(latestAnswer.id));
        if (latestQuestion) addSyncedRecord(latestQuestion.content, latestAnswer.content, Number(latestAnswer.id));
      }
      state.lastEventId = events.reduce((max, event) => Math.max(max, Number(event.id) || 0), 0);
      state.syncInitialized = true;
    } else {
      const newEvents = events.filter((event) => Number(event.id) > state.lastEventId).sort((a, b) => a.id - b.id);
      for (const event of newEvents) {
        if (event.role === 'assistant') {
          const question = [...events].reverse().find((item) => item.role === 'user' && Number(item.id) < Number(event.id));
          if (question) addSyncedRecord(question.content, event.content, Number(event.id));
        }
        state.lastEventId = Math.max(state.lastEventId, Number(event.id) || 0);
      }
    }
    $('syncState').textContent = '已连接 · 自动同步';
    $('liveState').textContent = '实时同步';
  } catch (_) {
    $('syncState').textContent = '未连接';
    $('liveState').textContent = '等待设备';
  } finally { state.syncing = false; }
}
$('deviceUrl').value = state.deviceUrl;
$('syncButton').addEventListener('click', () => { if (demoMode) return; state.deviceUrl = $('deviceUrl').value.trim().replace(/\/$/, ''); localStorage.setItem(deviceStorageKey, state.deviceUrl); state.syncInitialized = false; syncLearning(); });
if (demoMode) {
  $('deviceUrl').value = '鸡兔同笼演示档案';
  $('deviceUrl').disabled = true;
  $('syncButton').hidden = true;
  $('syncState').textContent = '演示数据';
  $('clearRecords').hidden = true;
  $('livePanel').hidden = false;
  $('liveState').textContent = '四级分步引导';
  $('liveQuestion').textContent = '鸡和兔一共有35个头、94条腿，请问鸡和兔分别有多少只？';
  $('liveAnswer').textContent = '瓦力先引导学生假设全部是鸡，算出70条腿；再比较实际腿数，找到多出的24条腿；接着聚焦“每换成1只兔子会多2条腿”，得到兔子12只、鸡23只；最后用35个头和94条腿进行双重验算。';
} else {
  setInterval(syncLearning, 2000);
}
render();
