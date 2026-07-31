/*
 * LanTaskmgr - client for both the login page and the manager page.
 *
 * Deliberately ES5 + XMLHttpRequest: this is served to whatever phone happens
 * to be on the LAN, including old Android WebViews, and it must run without a
 * single byte from the internet. No frameworks, no polyfills, no build step.
 */
(function () {
  'use strict';

  /* ================================================================ */
  /* i18n                                                             */
  /* ================================================================ */

  var STR = {
    EN: {
      title: 'LanTaskmgr',
      loginSub: 'Sign in to manage this PC',
      enterPw: 'Password',
      login: 'Log in',
      loggingIn: 'Signing in\u2026',
      foot: 'Running on your LAN only',
      badPw: 'Wrong password',
      blocked: 'Too many attempts. Restart the app on the PC.',
      netErr: 'Cannot reach the PC',
      logout: 'Log out',
      sortCpu: 'CPU',
      sortMem: 'RAM',
      sortName: 'Name',
      filter: 'Filter',
      tapToKill: 'Tap a process to end it.',
      noMatch: 'Nothing matches.',
      colMem: 'RAM',
      colCpu: 'CPU',
      colCount: 'Count',
      cancel: 'Cancel',
      endTask: 'End task',
      ending: 'Ending\u2026',
      ramOf: '{used} of {total} used',
      instances: '{n} instances',
      tagWin: 'APP',
      tagSys: 'SYS',
      protectedWarn: 'Windows needs this process. It cannot be ended.',
      systemWarn: 'System process. Ending it may destabilise Windows.',
      killedOk: 'Ended {name}',
      killedPartial: 'Partly ended {name}',
      killedProtected: 'Protected \u2014 refused',
      killedGone: 'Already gone',
      killedDenied: 'Access denied',
      offline: 'Connection lost \u2014 retrying\u2026'
    },
    CN: {
      title: '局域网任务管理器',
      loginSub: '登录以管理这台电脑',
      enterPw: '密码',
      login: '登录',
      loggingIn: '登录中\u2026',
      foot: '仅在你的局域网内运行',
      badPw: '密码错误',
      blocked: '尝试次数过多，请在电脑上重启程序。',
      netErr: '无法连接到电脑',
      logout: '退出',
      sortCpu: 'CPU',
      sortMem: '内存',
      sortName: '名称',
      filter: '筛选',
      tapToKill: '点击进程即可结束它。',
      noMatch: '没有匹配的进程。',
      colMem: '内存',
      colCpu: 'CPU',
      colCount: '实例',
      cancel: '取消',
      endTask: '结束任务',
      ending: '正在结束\u2026',
      ramOf: '已用 {used} / 共 {total}',
      instances: '{n} 个实例',
      tagWin: '应用',
      tagSys: '系统',
      protectedWarn: 'Windows 依赖此进程，无法结束。',
      systemWarn: '系统进程，结束后可能导致系统不稳定。',
      killedOk: '已结束 {name}',
      killedPartial: '部分结束 {name}',
      killedProtected: '受保护进程，已拒绝',
      killedGone: '进程已不存在',
      killedDenied: '权限不足',
      offline: '连接已断开，正在重试\u2026'
    },
    TW: {
      title: '區域網路工作管理員',
      loginSub: '登入以管理這台電腦',
      enterPw: '密碼',
      login: '登入',
      loggingIn: '登入中\u2026',
      foot: '僅在你的區域網路內執行',
      badPw: '密碼錯誤',
      blocked: '嘗試次數過多，請在電腦上重新啟動程式。',
      netErr: '無法連線到電腦',
      logout: '登出',
      sortCpu: 'CPU',
      sortMem: '記憶體',
      sortName: '名稱',
      filter: '篩選',
      tapToKill: '點擊處理程序即可結束它。',
      noMatch: '沒有符合的處理程序。',
      colMem: '記憶體',
      colCpu: 'CPU',
      colCount: '執行個體',
      cancel: '取消',
      endTask: '結束工作',
      ending: '正在結束\u2026',
      ramOf: '已用 {used} / 共 {total}',
      instances: '{n} 個執行個體',
      tagWin: '應用程式',
      tagSys: '系統',
      protectedWarn: 'Windows 需要此處理程序，無法結束。',
      systemWarn: '系統處理程序，結束後可能導致系統不穩定。',
      killedOk: '已結束 {name}',
      killedPartial: '部分結束 {name}',
      killedProtected: '受保護的處理程序，已拒絕',
      killedGone: '處理程序已不存在',
      killedDenied: '權限不足',
      offline: '連線中斷，正在重試\u2026'
    }
  };

  var HTML_LANG = { EN: 'en', CN: 'zh-CN', TW: 'zh-TW' };

  function cookie(name) {
    var parts = document.cookie ? document.cookie.split(';') : [];
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i];
      while (p.charAt(0) === ' ') { p = p.substring(1); }
      if (p.indexOf(name + '=') === 0) { return p.substring(name.length + 1); }
    }
    return '';
  }

  var lang = cookie('ltm_lang');
  if (!STR[lang]) {
    /* The PC did not tell us; fall back to whatever the phone prefers. */
    var nav = (navigator.language || 'en').toLowerCase();
    lang = nav.indexOf('zh') !== 0 ? 'EN'
         : (nav.indexOf('tw') > 0 || nav.indexOf('hk') > 0 || nav.indexOf('hant') > 0) ? 'TW'
         : 'CN';
  }
  var T = STR[lang];

  function t(key, vars) {
    var s = T[key];
    if (s === undefined) { return key; }
    if (vars) {
      for (var k in vars) {
        if (Object.prototype.hasOwnProperty.call(vars, k)) {
          s = s.split('{' + k + '}').join(vars[k]);
        }
      }
    }
    return s;
  }

  function applyStatic() {
    document.documentElement.lang = HTML_LANG[lang] || 'en';
    var i, els = document.querySelectorAll('[data-i18n]');
    for (i = 0; i < els.length; i++) {
      els[i].textContent = t(els[i].getAttribute('data-i18n'));
    }
    els = document.querySelectorAll('[data-i18n-ph]');
    for (i = 0; i < els.length; i++) {
      els[i].placeholder = t(els[i].getAttribute('data-i18n-ph'));
    }
    if (T.title) { document.title = T.title; }
  }

  /* ================================================================ */
  /* Transport                                                        */
  /* ================================================================ */

  function post(path, body, done) {
    var xhr = new XMLHttpRequest();
    xhr.open('POST', path, true);
    xhr.timeout = 8000;
    /* text/plain keeps the request simple: no preflight, no parsing on the
     * C side, the body is the payload. */
    xhr.setRequestHeader('Content-Type', 'text/plain; charset=utf-8');
    xhr.onreadystatechange = function () {
      if (xhr.readyState === 4) {
        done(xhr.status, xhr.responseText || '');
      }
    };
    xhr.ontimeout = function () { done(0, ''); };
    xhr.onerror = function () { done(0, ''); };
    try {
      xhr.send(body === undefined ? '' : body);
    } catch (e) {
      done(0, '');
    }
  }

  /* ================================================================ */
  /* Formatting                                                       */
  /* ================================================================ */

  function bytes(n) {
    if (!n) { return '0 MB'; }
    var mb = n / 1048576;
    if (mb < 1) { return Math.round(n / 1024) + ' KB'; }
    if (mb < 1024) { return (mb < 10 ? mb.toFixed(1) : Math.round(mb)) + ' MB'; }
    return (mb / 1024).toFixed(mb / 1024 < 10 ? 2 : 1) + ' GB';
  }

  function pct(v) {
    return (v < 10 ? v.toFixed(1) : Math.round(v)) + '%';
  }

  /* ================================================================ */
  /* Login page                                                       */
  /* ================================================================ */

  function initLogin() {
    var form = document.getElementById('loginForm');
    var pw = document.getElementById('pw');
    var btn = document.getElementById('loginBtn');
    var msg = document.getElementById('loginMsg');
    var busy = false;

    form.onsubmit = function (ev) {
      ev.preventDefault();
      if (busy) { return; }
      busy = true;
      btn.disabled = true;
      btn.textContent = t('loggingIn');
      msg.textContent = '';

      post('/dologin', pw.value, function (status, text) {
        if (status === 200 && text === 'ok') {
          location.replace('/');
          return;
        }
        busy = false;
        btn.disabled = false;
        btn.textContent = t('login');
        pw.value = '';
        msg.textContent = status === 0 ? t('netErr')
                        : status === 403 ? t('blocked')
                        : t('badPw');
        if (status !== 403) { pw.focus(); }
      });
    };

    setTimeout(function () { pw.focus(); }, 60);
  }

  /* ================================================================ */
  /* Manager page                                                     */
  /* ================================================================ */

  function initManager() {
    var listEl = document.getElementById('tasklist');
    var emptyEl = document.getElementById('empty');
    var filterEl = document.getElementById('filter');
    var memBar = document.getElementById('memBar');
    var memLabel = document.getElementById('memLabel');
    var chips = document.querySelectorAll('.chip');
    var sheet = document.getElementById('sheet');
    var toastEl = document.getElementById('toast');

    var POLL_MS = 2000;
    var sortBy = 'cpu';
    var filterText = '';
    var rows = {};        /* image name -> { el, nm, sub, mem, cpu, tag, cnt } */
    var data = [];
    var timer = null;
    var inFlight = false;
    var misses = 0;
    var selected = null;
    var toastTimer = null;

    try {
      var saved = localStorage.getItem('ltm_sort');
      if (saved === 'cpu' || saved === 'mem' || saved === 'name') { sortBy = saved; }
    } catch (e) { /* private mode: just use the default */ }

    /* ---- toast ---------------------------------------------------- */

    function toast(text, bad, sticky) {
      toastEl.textContent = text;
      toastEl.className = 'toast' + (bad ? ' bad' : '') + (sticky ? ' sticky' : '');
      toastEl.hidden = false;
      if (toastTimer) { clearTimeout(toastTimer); toastTimer = null; }
      if (!sticky) {
        toastTimer = setTimeout(function () { toastEl.hidden = true; }, 2200);
      }
    }

    function hideToast() {
      if (toastTimer) { clearTimeout(toastTimer); toastTimer = null; }
      toastEl.hidden = true;
    }

    /* ---- sorting / filtering -------------------------------------- */

    function compare(a, b) {
      if (sortBy === 'name') {
        var an = a.n.toLowerCase(), bn = b.n.toLowerCase();
        return an < bn ? -1 : an > bn ? 1 : 0;
      }
      if (sortBy === 'mem') {
        if (b.m !== a.m) { return b.m - a.m; }
      } else {
        if (b.p !== a.p) { return b.p - a.p; }
        if (b.m !== a.m) { return b.m - a.m; }
      }
      return a.n < b.n ? -1 : 1;
    }

    function visible() {
      var out = [], i;
      for (i = 0; i < data.length; i++) {
        var d = data[i];
        if (filterText &&
            d.n.toLowerCase().indexOf(filterText) < 0 &&
            (!d.t || d.t.toLowerCase().indexOf(filterText) < 0)) {
          continue;
        }
        out.push(d);
      }
      out.sort(compare);
      return out;
    }

    /* ---- row construction ----------------------------------------- */

    function makeRow(d) {
      var el = document.createElement('button');
      el.type = 'button';
      el.className = 'row';

      var nm = document.createElement('span');
      nm.className = 'nm';
      var nameText = document.createElement('span');
      nm.appendChild(nameText);
      var tag = document.createElement('span');
      tag.className = 'tag';
      tag.hidden = true;
      nm.appendChild(tag);
      var cnt = document.createElement('span');
      cnt.className = 'tag';
      cnt.hidden = true;
      nm.appendChild(cnt);

      var sub = document.createElement('span');
      sub.className = 'sub';

      var stats = document.createElement('span');
      stats.className = 'stats';
      var mem = document.createElement('b');
      var cpu = document.createElement('i');
      stats.appendChild(mem);
      stats.appendChild(cpu);

      el.appendChild(nm);
      el.appendChild(sub);
      el.appendChild(stats);

      var r = { el: el, name: nameText, tag: tag, cnt: cnt,
                sub: sub, mem: mem, cpu: cpu, cache: {} };

      el.onclick = function () { openSheet(d.n); };
      return r;
    }

    /* Only touch the DOM when a value actually changed - this runs every two
     * seconds on a phone and layout thrash is the one thing that would make
     * it feel heavy. */
    function paint(r, d) {
      var c = r.cache;

      if (c.n !== d.n) { r.name.textContent = d.n; c.n = d.n; }

      var sub = d.t || '';
      if (c.sub !== sub) { r.sub.textContent = sub; c.sub = sub; }

      var m = bytes(d.m);
      if (c.m !== m) { r.mem.textContent = m; c.m = m; }

      var p = pct(d.p);
      if (c.p !== p) {
        r.cpu.textContent = p;
        r.cpu.className = d.p >= 25 ? 'hot' : d.p >= 8 ? 'busy' : '';
        c.p = p;
      }

      var klass = d.k ? 'sys' : d.c === 2 ? 'sys' : d.c === 1 ? 'win' : '';
      if (c.klass !== klass) {
        if (klass) {
          r.tag.className = 'tag ' + klass;
          r.tag.textContent = klass === 'sys' ? t('tagSys') : t('tagWin');
          r.tag.hidden = false;
        } else {
          r.tag.hidden = true;
        }
        c.klass = klass;
      }

      var n = d.i > 1 ? '\u00d7' + d.i : '';
      if (c.i !== n) {
        r.cnt.textContent = n;
        r.cnt.hidden = !n;
        c.i = n;
      }

      var locked = !!d.k;
      if (c.locked !== locked) {
        if (locked) { r.el.classList.add('is-locked'); }
        else { r.el.classList.remove('is-locked'); }
        c.locked = locked;
      }
    }

    function render() {
      var items = visible();
      var seen = {}, i;

      emptyEl.hidden = items.length > 0;

      for (i = 0; i < items.length; i++) {
        var d = items[i];
        var r = rows[d.n];
        if (!r) {
          r = makeRow(d);
          rows[d.n] = r;
        }
        paint(r, d);
        seen[d.n] = 1;

        /* Move into place only if it is not already there. */
        var at = listEl.childNodes[i];
        if (at !== r.el) { listEl.insertBefore(r.el, at || null); }
      }

      for (var key in rows) {
        if (Object.prototype.hasOwnProperty.call(rows, key) && !seen[key]) {
          if (rows[key].el.parentNode) {
            listEl.removeChild(rows[key].el);
          }
          delete rows[key];
        }
      }
    }

    function find(name) {
      for (var i = 0; i < data.length; i++) {
        if (data[i].n === name) { return data[i]; }
      }
      return null;
    }

    /* ---- polling --------------------------------------------------- */

    function schedule(ms) {
      if (timer) { clearTimeout(timer); }
      timer = setTimeout(poll, ms);
    }

    function poll() {
      if (inFlight || document.hidden) { return; }
      inFlight = true;

      post('/list', '', function (status, text) {
        inFlight = false;

        if (status === 401) { location.replace('/'); return; }

        if (status !== 200) {
          misses++;
          if (misses >= 2) { toast(t('offline'), true, true); }
          /* Back off so a sleeping PC does not get hammered. */
          schedule(Math.min(POLL_MS * (1 + misses), 10000));
          return;
        }

        var parsed;
        try {
          parsed = JSON.parse(text);
        } catch (e) {
          schedule(POLL_MS);
          return;
        }

        if (misses >= 2) { hideToast(); }
        misses = 0;

        if (parsed.mem) {
          var pc = parsed.mem.pct || 0;
          memBar.style.width = pc + '%';
          memBar.className = pc >= 85 ? 'high' : pc >= 65 ? 'mid' : '';
          memLabel.textContent = pc + '%  \u00b7  ' +
            t('ramOf', { used: bytes(parsed.mem.used), total: bytes(parsed.mem.total) });
        }

        data = parsed.list || [];
        render();
        if (selected) { refreshSheet(); }
        schedule(POLL_MS);
      });
    }

    /* ---- kill sheet ------------------------------------------------ */

    var sheetName = document.getElementById('sheetName');
    var sheetTitle = document.getElementById('sheetTitle');
    var sheetMem = document.getElementById('sheetMem');
    var sheetCpu = document.getElementById('sheetCpu');
    var sheetCount = document.getElementById('sheetCount');
    var sheetWarn = document.getElementById('sheetWarn');
    var sheetKill = document.getElementById('sheetKill');
    var sheetCancel = document.getElementById('sheetCancel');

    function refreshSheet() {
      var d = find(selected);
      if (!d) { closeSheet(); return; }
      sheetName.textContent = d.n;
      sheetTitle.textContent = d.t || '';
      sheetTitle.hidden = !d.t;
      sheetMem.textContent = bytes(d.m);
      sheetCpu.textContent = pct(d.p);
      sheetCount.textContent = String(d.i);

      if (d.k) {
        sheetWarn.textContent = t('protectedWarn');
        sheetWarn.hidden = false;
        sheetKill.disabled = true;
      } else if (d.c === 2) {
        sheetWarn.textContent = t('systemWarn');
        sheetWarn.hidden = false;
        sheetKill.disabled = false;
      } else {
        sheetWarn.hidden = true;
        sheetKill.disabled = false;
      }
    }

    function openSheet(name) {
      selected = name;
      sheetKill.textContent = t('endTask');
      refreshSheet();
      if (selected) { sheet.hidden = false; }
    }

    function closeSheet() {
      selected = null;
      sheet.hidden = true;
    }

    sheet.onclick = function (ev) {
      if (ev.target === sheet) { closeSheet(); }
    };
    sheetCancel.onclick = closeSheet;

    sheetKill.onclick = function () {
      if (!selected) { return; }
      var name = selected;
      sheetKill.disabled = true;
      sheetKill.textContent = t('ending');

      post('/kill', name, function (status, text) {
        closeSheet();
        sheetKill.textContent = t('endTask');

        if (status === 200 && text === 'ok') {
          toast(t('killedOk', { name: name }));
        } else if (status === 200 && text === 'partial') {
          toast(t('killedPartial', { name: name }), true);
        } else if (status === 404) {
          toast(t('killedGone'), true);
        } else if (status === 403 && text === 'protected') {
          toast(t('killedProtected'), true);
        } else if (status === 401) {
          location.replace('/');
          return;
        } else if (status === 0) {
          toast(t('netErr'), true);
        } else {
          toast(t('killedDenied'), true);
        }

        /* Refresh straight away so the row disappears without waiting. */
        schedule(150);
      });
    };

    /* ---- controls --------------------------------------------------- */

    filterEl.oninput = function () {
      filterText = filterEl.value.toLowerCase();
      render();
    };

    for (var ci = 0; ci < chips.length; ci++) {
      chips[ci].onclick = function () {
        var want = this.getAttribute('data-sort');
        if (want === sortBy) { return; }
        sortBy = want;
        try { localStorage.setItem('ltm_sort', sortBy); } catch (e) { /* ignore */ }
        for (var k = 0; k < chips.length; k++) {
          if (chips[k].getAttribute('data-sort') === sortBy) {
            chips[k].classList.add('is-on');
          } else {
            chips[k].classList.remove('is-on');
          }
        }
        render();
      };
      if (chips[ci].getAttribute('data-sort') === sortBy) {
        chips[ci].classList.add('is-on');
      } else {
        chips[ci].classList.remove('is-on');
      }
    }

    document.getElementById('logoutBtn').onclick = function () {
      post('/logout', '', function () { location.replace('/'); });
    };

    /* Stop polling in the background: no point burning phone battery and PC
     * cycles for a screen nobody is looking at. */
    document.addEventListener('visibilitychange', function () {
      if (document.hidden) {
        if (timer) { clearTimeout(timer); timer = null; }
      } else {
        misses = 0;
        poll();
      }
    });

    poll();
  }

  /* ================================================================ */

  applyStatic();

  if (document.body.className.indexOf('page-login') >= 0) {
    initLogin();
  } else {
    initManager();
  }
})();
