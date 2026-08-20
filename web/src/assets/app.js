(() => {
  'use strict';

  const WEB_VERSION = '1.1.1';
  const DATA_URL = './data/catalog.generated.json';
  const BUILD_INFO_URL = './data/build-info.json';
  const SEARCH_DEBOUNCE_MS = 90;
  const SEARCH_HISTORY_LIMIT = 32;
  const RECENT_LIMIT = 17;
  const COMMON_AUTO_ADD_THRESHOLD = 5;
  const BLOCK_ROWS = 18;

  const STORAGE = Object.freeze({
    recent: 'yesymbol.web.recent.v1',
    common: 'yesymbol.web.common.v2',
    commonDefaultsSeen: 'yesymbol.web.common-defaults-seen.v1',
    usage: 'yesymbol.web.usage.v1',
    custom: 'yesymbol.web.custom.v1',
    searchHistory: 'yesymbol.web.search-history.v1',
    settings: 'yesymbol.web.settings.v1'
  });

  const state = {
    data: null,
    buildInfo: null,
    symbolByText: new Map(),
    categoryByName: new Map(),
    locationByCategory: new Map(),
    originByText: new Map(),
    searchable: [],
    recent: [],
    common: [],
    usage: {},
    custom: [],
    searchHistory: [],
    searchHistoryIndex: -1,
    searchDraft: '',
    settings: {
      copyOnClick: true,
      stickyHeader: true,
      recentOpen: true,
      emojiOpen: false,
      otherOpen: false
    },
    currentCategory: '常用符号',
    activeNav: '常用符号',
    currentMode: 'category',
    currentSearch: '',
    renderBlocks: [],
    renderBlockIndex: 0,
    searchResults: [],
    searchRenderIndex: 0,
    pendingTarget: null,
    selectedText: '',
    contextText: '',
    tooltipTimer: 0,
    searchTimer: 0,
    toastTimer: 0,
    draggingText: '',
    observer: null
  };

  const refs = {};

  document.addEventListener('DOMContentLoaded', init, { once: true });

  async function init() {
    cacheRefs();
    bindStaticEvents();
    refs.webVersion.textContent = `v${WEB_VERSION}`;

    try {
      const [dataResponse, buildInfoResponse] = await Promise.all([
        fetch(DATA_URL, { cache: 'no-store' }),
        fetch(BUILD_INFO_URL, { cache: 'no-store' }).catch(() => null)
      ]);
      if (!dataResponse.ok) {
        throw new Error(`读取 ${DATA_URL} 失败：HTTP ${dataResponse.status}`);
      }
      state.data = await dataResponse.json();
      if (buildInfoResponse && buildInfoResponse.ok) {
        state.buildInfo = await buildInfoResponse.json();
      }
      validateCatalog(state.data);
      prepareCatalog();
      loadUserState();
      applySettingsToUi();
      renderSidebar();
      renderRecent();
      selectCategory('常用符号', { preserveSearch: true });
      setupInfiniteObserver();
      updateDataBadge();
      refs.aboutDataCount.textContent = `${state.data.symbols.length.toLocaleString('zh-CN')} 个符号或序列`;
      refs.app.setAttribute('aria-busy', 'false');
      registerServiceWorker();
    } catch (error) {
      showFatalError(error);
    }
  }

  function cacheRefs() {
    const ids = [
      'app', 'topPanel', 'recentToggle', 'recentArrow', 'searchInput', 'searchClear',
      'copyToggle', 'stickyToggle', 'aboutButton', 'recentRow', 'recentGrid', 'clearRecent',
      'sidebar', 'viewTitle', 'viewMeta', 'viewActions', 'customEditor', 'customInput',
      'customAdd', 'symbolViewport', 'symbolContent', 'loadSentinel', 'emptyState',
      'statusSymbol', 'statusZh', 'statusCode', 'statusEn', 'statusCategory', 'dataBadge',
      'tooltip', 'contextMenu', 'ctxCommon', 'ctxJump', 'ctxDeleteCustom', 'toast',
      'aboutDialog', 'webVersion', 'aboutDataCount'
    ];
    for (const id of ids) refs[id] = document.getElementById(id);
  }

  function bindStaticEvents() {
    refs.recentToggle.addEventListener('click', () => {
      state.settings.recentOpen = !state.settings.recentOpen;
      saveSettings();
      applyRecentVisibility();
    });

    refs.searchInput.addEventListener('input', handleSearchInput);
    refs.searchInput.addEventListener('keydown', handleSearchKeydown);
    refs.searchClear.addEventListener('click', () => clearSearch(true));

    refs.copyToggle.addEventListener('change', () => {
      state.settings.copyOnClick = refs.copyToggle.checked;
      saveSettings();
    });
    refs.stickyToggle.addEventListener('change', () => {
      state.settings.stickyHeader = refs.stickyToggle.checked;
      saveSettings();
      applyStickySetting();
    });

    refs.clearRecent.addEventListener('click', () => {
      if (!state.recent.length) return;
      if (window.confirm('确定清空最近使用记录吗？')) {
        state.recent = [];
        saveJson(STORAGE.recent, state.recent);
        renderRecent();
        showToast('最近使用已清空');
      }
    });

    refs.sidebar.addEventListener('click', handleSidebarClick);
    refs.symbolContent.addEventListener('click', handleSymbolClick);
    refs.recentGrid.addEventListener('click', handleSymbolClick);
    refs.symbolContent.addEventListener('contextmenu', handleSymbolContextMenu);
    refs.recentGrid.addEventListener('contextmenu', handleSymbolContextMenu);
    refs.symbolContent.addEventListener('pointerover', handleSymbolPointerOver);
    refs.recentGrid.addEventListener('pointerover', handleSymbolPointerOver);
    refs.symbolContent.addEventListener('pointerout', handleSymbolPointerOut);
    refs.recentGrid.addEventListener('pointerout', handleSymbolPointerOut);
    refs.symbolContent.addEventListener('focusin', handleSymbolFocus);
    refs.recentGrid.addEventListener('focusin', handleSymbolFocus);

    refs.symbolContent.addEventListener('dragstart', handleDragStart);
    refs.symbolContent.addEventListener('dragover', handleDragOver);
    refs.symbolContent.addEventListener('dragleave', handleDragLeave);
    refs.symbolContent.addEventListener('drop', handleDrop);
    refs.symbolContent.addEventListener('dragend', handleDragEnd);

    refs.customAdd.addEventListener('click', addCustomFromInput);
    refs.customInput.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') addCustomFromInput();
    });

    refs.ctxCommon.addEventListener('click', () => {
      const text = state.contextText;
      hideContextMenu();
      if (!text) return;
      if (state.common.includes(text)) removeFromCommon(text);
      else addToCommon(text, true);
    });
    refs.ctxJump.addEventListener('click', () => {
      const text = state.contextText;
      hideContextMenu();
      if (text) jumpToOrigin(text);
    });
    refs.ctxDeleteCustom.addEventListener('click', () => {
      const text = state.contextText;
      hideContextMenu();
      if (text) removeCustom(text);
    });

    refs.aboutButton.addEventListener('click', () => refs.aboutDialog.showModal());

    document.addEventListener('pointerdown', (event) => {
      if (!refs.contextMenu.hidden && !refs.contextMenu.contains(event.target)) hideContextMenu();
    });
    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') {
        hideContextMenu();
        hideTooltip();
      }
    });
    window.addEventListener('resize', () => {
      hideContextMenu();
      hideTooltip();
    });
  }

  function validateCatalog(data) {
    if (!data || !Array.isArray(data.categories) || !Array.isArray(data.symbols)) {
      throw new Error('catalog.generated.json 格式不正确：缺少 categories 或 symbols。');
    }
    if (!Array.isArray(data.ui_categories) || !Array.isArray(data.default_common)) {
      throw new Error('catalog.generated.json 格式不正确：缺少 ui_categories 或 default_common。');
    }
  }

  function prepareCatalog() {
    state.symbolByText = new Map(state.data.symbols.map(record => [record.text, record]));
    state.categoryByName = new Map(state.data.categories.map(category => [category.name, category]));
    state.locationByCategory = new Map();

    for (const category of state.data.categories) {
      const map = new Map();
      for (const group of category.groups || []) {
        if (group.spacer || !Array.isArray(group.items) || !group.items.length) continue;
        const cols = Math.max(1, Number(group.cols) || 12);
        const baseRow = Math.max(0, Number(group.row) || 0);
        group.items.forEach((text, index) => {
          if (map.has(text)) return;
          map.set(text, {
            category: category.name,
            group: group.title || '',
            row: baseRow + Math.floor(index / cols) + 1,
            item: (index % cols) + 1
          });
        });
      }
      state.locationByCategory.set(category.name, map);
    }

    const groupParents = new Set(Object.keys(state.data.ui_category_groups || {}));
    const uiOrder = state.data.ui_categories.filter(name =>
      !['常用符号', '全部符号', '自定义'].includes(name) && !groupParents.has(name)
    );
    for (const name of uiOrder) {
      const map = state.locationByCategory.get(name);
      if (!map) continue;
      for (const [text, location] of map) {
        if (!state.originByText.has(text)) state.originByText.set(text, location);
      }
    }

    const allMap = state.locationByCategory.get('全部符号');
    if (allMap) {
      for (const [text, location] of allMap) {
        if (!state.originByText.has(text)) state.originByText.set(text, location);
      }
    }

    state.searchable = state.data.symbols.map(record => {
      const origin = state.originByText.get(record.text) || {
        category: '全部符号', group: '', row: 0, item: 0
      };
      const code = formatCodePoints(record.text);
      return {
        record,
        origin,
        code,
        haystack: [
          record.text,
          record.name_zh || '',
          record.name_en || record.name || '',
          origin.category,
          origin.group,
          code,
          code.replaceAll('U+', '').replaceAll(' ', '')
        ].join('\n').toLocaleLowerCase('zh-CN')
      };
    });
  }

  function loadUserState() {
    state.recent = uniqueStrings(loadJson(STORAGE.recent, [])).slice(0, RECENT_LIMIT);
    state.custom = uniqueStrings(loadJson(STORAGE.custom, []));
    state.usage = loadJson(STORAGE.usage, {});
    if (!state.usage || typeof state.usage !== 'object' || Array.isArray(state.usage)) state.usage = {};
    state.searchHistory = uniqueStrings(loadJson(STORAGE.searchHistory, [])).slice(0, SEARCH_HISTORY_LIMIT);

    const loadedSettings = loadJson(STORAGE.settings, {});
    if (loadedSettings && typeof loadedSettings === 'object') {
      state.settings = { ...state.settings, ...loadedSettings };
    }

    const defaults = uniqueStrings(state.data.default_common);
    const seenDefaults = new Set(uniqueStrings(loadJson(STORAGE.commonDefaultsSeen, [])));
    const storedCommon = loadJson(STORAGE.common, null);
    state.common = Array.isArray(storedCommon) ? uniqueStrings(storedCommon) : [...defaults];

    for (const text of defaults) {
      if (!seenDefaults.has(text) && !state.common.includes(text)) state.common.push(text);
      seenDefaults.add(text);
    }
    saveJson(STORAGE.commonDefaultsSeen, [...seenDefaults]);
    saveJson(STORAGE.common, state.common);
  }

  function applySettingsToUi() {
    refs.copyToggle.checked = Boolean(state.settings.copyOnClick);
    refs.stickyToggle.checked = Boolean(state.settings.stickyHeader);
    applyStickySetting();
    applyRecentVisibility();
  }

  function applyStickySetting() {
    refs.topPanel.classList.toggle('is-sticky', Boolean(state.settings.stickyHeader));
  }

  function applyRecentVisibility() {
    const open = Boolean(state.settings.recentOpen);
    refs.recentRow.hidden = !open;
    refs.recentToggle.setAttribute('aria-expanded', String(open));
    refs.recentArrow.textContent = open ? '▼' : '▶';
  }

  function renderSidebar() {
    refs.sidebar.replaceChildren();
    const groups = state.data.ui_category_groups || {};
    const childToParent = new Map();
    for (const [parent, children] of Object.entries(groups)) {
      for (const child of children || []) childToParent.set(child, parent);
    }
    const counts = getCategoryCounts();

    for (const name of state.data.ui_categories) {
      const parentName = childToParent.get(name);
      if (parentName && !isGroupOpen(parentName)) continue;
      const isParent = Object.hasOwn(groups, name);
      const isChild = Boolean(parentName);
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'category-button';
      button.dataset.category = name;
      button.classList.toggle('is-parent', isParent);
      button.classList.toggle('is-child', isChild);
      button.classList.toggle('is-active', state.activeNav === name);
      if (isParent) {
        const open = isGroupOpen(name);
        button.setAttribute('aria-expanded', String(open));
        button.textContent = `${name} ${open ? 'v' : '>'}`;
      } else {
        const label = document.createElement('span');
        label.textContent = name;
        button.append(label);
        const count = counts.get(name);
        if (Number.isFinite(count) && count > 0) {
          const countEl = document.createElement('span');
          countEl.className = 'count';
          countEl.textContent = count.toLocaleString('zh-CN');
          button.append(countEl);
        }
      }
      refs.sidebar.append(button);
    }
  }

  function groupSettingName(parent) {
    return parent === 'Emoji' ? 'emojiOpen' : 'otherOpen';
  }

  function isGroupOpen(parent) {
    return Boolean(state.settings[groupSettingName(parent)]);
  }

  function setGroupOpen(parent, open) {
    state.settings[groupSettingName(parent)] = Boolean(open);
  }

  function getCategoryCounts() {
    const counts = new Map();
    counts.set('常用符号', state.common.length);
    counts.set('自定义', state.custom.length);
    for (const category of state.data.categories) {
      counts.set(category.name, (category.groups || []).reduce((sum, group) => sum + (group.items?.length || 0), 0));
    }
    return counts;
  }

  function handleSidebarClick(event) {
    const button = event.target.closest('.category-button');
    if (!button) return;
    const name = button.dataset.category;
    const children = state.data.ui_category_groups?.[name];
    if (Array.isArray(children)) {
      const opening = !isGroupOpen(name);
      setGroupOpen(name, opening);
      saveSettings();
      if (opening && children.length) selectCategory(children[0]);
      else {
        state.activeNav = name;
        renderSidebar();
      }
      return;
    }
    selectCategory(name);
  }

  function selectCategory(name, options = {}) {
    if (!state.data.ui_categories.includes(name) && !['常用符号', '自定义'].includes(name)) return;
    state.currentCategory = name;
    state.activeNav = name;
    state.currentMode = 'category';
    state.pendingTarget = options.target || null;
    if (!options.preserveSearch) clearSearch(false);

    for (const [parent, children] of Object.entries(state.data.ui_category_groups || {})) {
      if (children.includes(name) && !isGroupOpen(parent)) {
        setGroupOpen(parent, true);
        saveSettings();
      }
    }

    renderSidebar();
    renderCurrentCategory();
  }

  function renderCurrentCategory() {
    resetContent();
    refs.customEditor.hidden = state.currentCategory !== '自定义';
    refs.viewActions.replaceChildren();

    if (state.currentCategory === '常用符号') {
      refs.viewTitle.textContent = '常用符号';
      refs.viewMeta.textContent = `${state.common.length} 个 · 固定顺序 · 拖动可排序 · 使用 ${COMMON_AUTO_ADD_THRESHOLD} 次自动加入`;
      renderDynamicGrid(state.common, { common: true });
      return;
    }

    if (state.currentCategory === '自定义') {
      refs.viewTitle.textContent = '自定义';
      refs.viewMeta.textContent = `${state.custom.length} 个 · 数据仅保存在当前浏览器`;
      renderDynamicGrid(state.custom, { custom: true });
      return;
    }

    const category = state.categoryByName.get(state.currentCategory);
    if (!category) {
      showEmpty('该分类尚无可用数据。');
      return;
    }

    const count = (category.groups || []).reduce((sum, group) => sum + (group.items?.length || 0), 0);
    refs.viewTitle.textContent = category.name;
    refs.viewMeta.textContent = category.desc || `${count.toLocaleString('zh-CN')} 个符号或序列`;

    if (!count) {
      showEmpty(category.desc || '该分类暂未收录字符。');
      return;
    }

    state.renderBlocks = buildCategoryBlocks(category);
    appendCategoryBlocks(true);
  }

  function buildCategoryBlocks(category) {
    const blocks = [];
    for (const group of category.groups || []) {
      if (group.spacer) {
        blocks.push({ spacer: true });
        continue;
      }
      const items = Array.isArray(group.items) ? group.items : [];
      if (!items.length) continue;
      const cols = clamp(Number(group.cols) || 12, 1, 12);
      const chunkSize = cols * BLOCK_ROWS;
      for (let offset = 0; offset < items.length; offset += chunkSize) {
        blocks.push({
          category: category.name,
          group: group.title || '',
          title: offset === 0 ? (group.title || '') : '',
          cols,
          items: items.slice(offset, offset + chunkSize),
          itemOffset: offset,
          rowBase: Math.max(0, Number(group.row) || 0)
        });
      }
    }
    return blocks;
  }

  function appendCategoryBlocks(initial = false) {
    if (state.currentMode !== 'category' || !state.renderBlocks.length) return;
    let symbolBudget = initial ? 520 : 420;
    while (state.renderBlockIndex < state.renderBlocks.length && symbolBudget > 0) {
      const block = state.renderBlocks[state.renderBlockIndex++];
      if (block.spacer) {
        const spacer = document.createElement('div');
        spacer.className = 'symbol-section is-spacer';
        refs.symbolContent.append(spacer);
        continue;
      }
      refs.symbolContent.append(createSection(block));
      symbolBudget -= block.items.length;
      if (state.pendingTarget && block.items.includes(state.pendingTarget)) {
        requestAnimationFrame(scrollPendingTargetIntoView);
      }
    }
    if (state.renderBlockIndex >= state.renderBlocks.length) refs.loadSentinel.hidden = true;
  }

  function createSection(block) {
    const section = document.createElement('section');
    section.className = 'symbol-section';
    if (block.title && block.title !== '_') {
      const title = document.createElement('h2');
      title.className = 'group-title';
      title.textContent = block.title;
      section.append(title);
    }
    const grid = document.createElement('div');
    grid.className = 'symbol-grid';
    grid.style.setProperty('--cols', String(block.cols));
    for (const text of block.items) {
      grid.append(createSymbolButton(text, {
        contextCategory: block.category,
        group: block.group
      }));
    }
    section.append(grid);
    return section;
  }

  function renderDynamicGrid(items, options = {}) {
    refs.loadSentinel.hidden = true;
    if (!items.length) {
      showEmpty(options.common ? '还没有常用符号。右键任意符号可以加入常用。' : '还没有自定义符号。');
      return;
    }
    const grid = document.createElement('div');
    grid.className = 'symbol-grid';
    grid.style.setProperty('--cols', '12');
    for (const text of items) {
      grid.append(createSymbolButton(text, {
        contextCategory: options.common ? '常用符号' : '自定义',
        draggable: Boolean(options.common),
        removable: true,
        custom: Boolean(options.custom)
      }));
    }
    refs.symbolContent.append(grid);
  }

  function createSymbolButton(text, options = {}) {
    const info = getSymbolInfo(text, options.contextCategory);
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'symbol-button';
    button.classList.toggle('is-emoji', Boolean(info.emoji));
    button.classList.toggle('is-selected', state.selectedText === text);
    button.dataset.symbol = text;
    button.dataset.contextCategory = options.contextCategory || '';
    button.setAttribute('aria-label', `${info.nameZh || '符号'} ${text}`);
    button.textContent = text;
    if (options.draggable) button.draggable = true;
    if (options.removable) {
      const badge = document.createElement('span');
      badge.className = 'remove-badge';
      badge.setAttribute('aria-hidden', 'true');
      badge.textContent = '×';
      button.append(badge);
    }
    if (options.custom) button.dataset.custom = '1';
    return button;
  }

  function renderRecent() {
    refs.recentGrid.replaceChildren();
    for (const text of state.recent.slice(0, RECENT_LIMIT)) {
      refs.recentGrid.append(createSymbolButton(text, { contextCategory: '最近使用' }));
    }
  }

  function handleSymbolClick(event) {
    const button = event.target.closest('.symbol-button');
    if (!button) return;
    const text = button.dataset.symbol;
    if (event.target.closest('.remove-badge')) {
      if (button.dataset.contextCategory === '常用符号') removeFromCommon(text);
      else if (button.dataset.custom === '1') removeCustom(text);
      return;
    }
    selectSymbol(text);
    if (state.settings.copyOnClick) void copyAndRecord(text);
  }

  function selectSymbol(text) {
    state.selectedText = text;
    document.querySelectorAll('.symbol-button.is-selected').forEach(el => {
      el.classList.toggle('is-selected', el.dataset.symbol === text);
    });
    updateStatus(text);
  }

  async function copyAndRecord(text) {
    try {
      await writeClipboard(text);
      recordUse(text);
      showToast(`已复制：${text}`);
    } catch (error) {
      console.error(error);
      showToast('复制失败，请检查浏览器剪贴板权限');
    }
  }

  async function writeClipboard(text) {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return;
    }
    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.style.position = 'fixed';
    textarea.style.opacity = '0';
    textarea.style.pointerEvents = 'none';
    document.body.append(textarea);
    textarea.select();
    const ok = document.execCommand('copy');
    textarea.remove();
    if (!ok) throw new Error('document.execCommand(copy) failed');
  }

  function recordUse(text) {
    state.recent = [text, ...state.recent.filter(item => item !== text)].slice(0, RECENT_LIMIT);
    saveJson(STORAGE.recent, state.recent);
    renderRecent();

    if (!state.common.includes(text)) {
      const count = Math.max(0, Number(state.usage[text]) || 0) + 1;
      state.usage[text] = count;
      if (count >= COMMON_AUTO_ADD_THRESHOLD) {
        state.common.push(text);
        delete state.usage[text];
        saveJson(STORAGE.common, state.common);
        renderSidebar();
        showToast(`已自动加入常用：${text}`);
        if (state.currentCategory === '常用符号' && !state.currentSearch) renderCurrentCategory();
      }
      saveJson(STORAGE.usage, state.usage);
    }
  }

  function addToCommon(text, manual = false) {
    if (!text || state.common.includes(text)) return;
    state.common.push(text);
    delete state.usage[text];
    saveJson(STORAGE.common, state.common);
    saveJson(STORAGE.usage, state.usage);
    renderSidebar();
    if (state.currentCategory === '常用符号' && !state.currentSearch) renderCurrentCategory();
    showToast(manual ? `已加入常用：${text}` : `已自动加入常用：${text}`);
  }

  function removeFromCommon(text) {
    const next = state.common.filter(item => item !== text);
    if (next.length === state.common.length) return;
    state.common = next;
    state.usage[text] = 0;
    saveJson(STORAGE.common, state.common);
    saveJson(STORAGE.usage, state.usage);
    renderSidebar();
    if (state.currentCategory === '常用符号' && !state.currentSearch) renderCurrentCategory();
    showToast(`已从常用删除：${text}`);
  }

  function handleDragStart(event) {
    const button = event.target.closest('.symbol-button[draggable="true"]');
    if (!button || state.currentCategory !== '常用符号' || state.currentSearch) return;
    state.draggingText = button.dataset.symbol;
    button.classList.add('is-dragging');
    event.dataTransfer.effectAllowed = 'move';
    event.dataTransfer.setData('text/plain', state.draggingText);
  }

  function handleDragOver(event) {
    if (!state.draggingText) return;
    const button = event.target.closest('.symbol-button[draggable="true"]');
    if (!button || button.dataset.symbol === state.draggingText) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
    clearDragOver();
    button.classList.add('drag-over');
  }

  function handleDragLeave(event) {
    const button = event.target.closest('.symbol-button.drag-over');
    if (button && !button.contains(event.relatedTarget)) button.classList.remove('drag-over');
  }

  function handleDrop(event) {
    if (!state.draggingText) return;
    const button = event.target.closest('.symbol-button[draggable="true"]');
    if (!button) return;
    event.preventDefault();
    const target = button.dataset.symbol;
    const from = state.common.indexOf(state.draggingText);
    const to = state.common.indexOf(target);
    if (from >= 0 && to >= 0 && from !== to) {
      const [moved] = state.common.splice(from, 1);
      state.common.splice(to, 0, moved);
      saveJson(STORAGE.common, state.common);
      renderCurrentCategory();
      showToast('常用符号顺序已保存');
    }
    handleDragEnd();
  }

  function handleDragEnd() {
    state.draggingText = '';
    document.querySelectorAll('.is-dragging, .drag-over').forEach(el => {
      el.classList.remove('is-dragging', 'drag-over');
    });
  }

  function clearDragOver() {
    document.querySelectorAll('.drag-over').forEach(el => el.classList.remove('drag-over'));
  }

  function handleSearchInput() {
    const query = refs.searchInput.value;
    refs.searchClear.hidden = !query;
    window.clearTimeout(state.searchTimer);
    state.searchTimer = window.setTimeout(() => runSearch(query), SEARCH_DEBOUNCE_MS);
  }

  function runSearch(rawQuery) {
    const query = rawQuery.trim();
    state.currentSearch = query;
    if (!query) {
      state.currentMode = 'category';
      renderCurrentCategory();
      return;
    }

    const normalized = query.toLocaleLowerCase('zh-CN');
    const compactCode = normalized.replace(/^u\+/, '').replace(/\s+/g, '');
    const scored = [];
    for (const item of state.searchable) {
      let score = 99;
      const record = item.record;
      const zh = (record.name_zh || '').toLocaleLowerCase('zh-CN');
      const en = (record.name_en || record.name || '').toLocaleLowerCase('zh-CN');
      const category = item.origin.category.toLocaleLowerCase('zh-CN');
      const codeCompact = item.code.toLocaleLowerCase('zh-CN').replaceAll('u+', '').replaceAll(' ', '');
      if (record.text === query) score = 0;
      else if (codeCompact === compactCode) score = 1;
      else if (zh.startsWith(normalized)) score = 2;
      else if (en.startsWith(normalized)) score = 3;
      else if (category.includes(normalized)) score = 4;
      else if (item.haystack.includes(normalized) || codeCompact.includes(compactCode)) score = 5;
      if (score < 99) scored.push({ ...item, score });
    }
    scored.sort((a, b) => a.score - b.score || a.origin.category.localeCompare(b.origin.category, 'zh-CN') || a.record.text.localeCompare(b.record.text, 'zh-CN'));
    state.searchResults = scored;
    state.currentMode = 'search';
    state.activeNav = '';
    renderSidebar();
    renderSearchResults();
  }

  function renderSearchResults() {
    resetContent();
    refs.customEditor.hidden = true;
    refs.viewTitle.textContent = '搜索结果';
    refs.viewMeta.textContent = `${state.searchResults.length.toLocaleString('zh-CN')} 个结果 · 右键可跳到原始位置`;
    if (!state.searchResults.length) {
      showEmpty();
      return;
    }
    appendSearchBatch(true);
  }

  function appendSearchBatch(initial = false) {
    if (state.currentMode !== 'search') return;
    const batchSize = initial ? 600 : 480;
    const end = Math.min(state.searchResults.length, state.searchRenderIndex + batchSize);
    if (state.searchRenderIndex >= end) return;

    let grid = refs.symbolContent.querySelector('.search-grid:last-child');
    if (!grid || grid.childElementCount >= batchSize) {
      grid = document.createElement('div');
      grid.className = 'search-grid';
      refs.symbolContent.append(grid);
    }
    for (let index = state.searchRenderIndex; index < end; index++) {
      const item = state.searchResults[index];
      grid.append(createSymbolButton(item.record.text, { contextCategory: '搜索结果' }));
    }
    state.searchRenderIndex = end;
    if (end >= state.searchResults.length) refs.loadSentinel.hidden = true;
  }

  function handleSearchKeydown(event) {
    if (event.key === 'Escape') {
      event.preventDefault();
      clearSearch(true);
      return;
    }
    if (event.key === 'Enter') {
      const query = refs.searchInput.value.trim();
      if (query) saveSearchHistory(query);
      return;
    }
    if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
    event.preventDefault();
    if (!state.searchHistory.length) return;

    if (state.searchHistoryIndex < 0) state.searchDraft = refs.searchInput.value;
    if (event.key === 'ArrowUp') {
      state.searchHistoryIndex = Math.min(state.searchHistory.length - 1, state.searchHistoryIndex + 1);
      refs.searchInput.value = state.searchHistory[state.searchHistoryIndex] || '';
    } else {
      state.searchHistoryIndex -= 1;
      refs.searchInput.value = state.searchHistoryIndex < 0
        ? state.searchDraft
        : (state.searchHistory[state.searchHistoryIndex] || '');
    }
    refs.searchClear.hidden = !refs.searchInput.value;
    runSearch(refs.searchInput.value);
    refs.searchInput.setSelectionRange(refs.searchInput.value.length, refs.searchInput.value.length);
  }

  function saveSearchHistory(query) {
    state.searchHistory = [query, ...state.searchHistory.filter(item => item !== query)].slice(0, SEARCH_HISTORY_LIMIT);
    state.searchHistoryIndex = -1;
    state.searchDraft = '';
    saveJson(STORAGE.searchHistory, state.searchHistory);
  }

  function clearSearch(focus) {
    window.clearTimeout(state.searchTimer);
    refs.searchInput.value = '';
    refs.searchClear.hidden = true;
    state.currentSearch = '';
    state.currentMode = 'category';
    state.searchHistoryIndex = -1;
    state.activeNav = state.currentCategory;
    renderSidebar();
    renderCurrentCategory();
    if (focus) refs.searchInput.focus();
  }

  function setupInfiniteObserver() {
    state.observer?.disconnect();
    state.observer = new IntersectionObserver(entries => {
      if (!entries.some(entry => entry.isIntersecting)) return;
      if (state.currentMode === 'search') appendSearchBatch(false);
      else appendCategoryBlocks(false);
    }, { root: refs.symbolViewport, rootMargin: '500px 0px' });
    state.observer.observe(refs.loadSentinel);
  }

  function resetContent() {
    refs.symbolContent.replaceChildren();
    refs.emptyState.hidden = true;
    refs.loadSentinel.hidden = false;
    refs.symbolViewport.scrollTop = 0;
    state.renderBlocks = [];
    state.renderBlockIndex = 0;
    state.searchRenderIndex = 0;
  }

  function showEmpty(message = '') {
    refs.symbolContent.replaceChildren();
    refs.loadSentinel.hidden = true;
    refs.emptyState.hidden = false;
    const span = refs.emptyState.querySelector('span');
    if (message) span.textContent = message;
  }

  function handleSymbolContextMenu(event) {
    const button = event.target.closest('.symbol-button');
    if (!button) return;
    event.preventDefault();
    const text = button.dataset.symbol;
    state.contextText = text;
    refs.ctxCommon.textContent = state.common.includes(text) ? '从常用符号删除' : '加入常用符号';
    refs.ctxCommon.hidden = false;
    refs.ctxJump.hidden = !state.originByText.has(text);
    refs.ctxDeleteCustom.hidden = !state.custom.includes(text);
    showContextMenu(event.clientX, event.clientY);
  }

  function showContextMenu(x, y) {
    refs.contextMenu.hidden = false;
    refs.contextMenu.style.left = `${x}px`;
    refs.contextMenu.style.top = `${y}px`;
    const rect = refs.contextMenu.getBoundingClientRect();
    if (rect.right > window.innerWidth - 8) refs.contextMenu.style.left = `${Math.max(8, window.innerWidth - rect.width - 8)}px`;
    if (rect.bottom > window.innerHeight - 8) refs.contextMenu.style.top = `${Math.max(8, window.innerHeight - rect.height - 8)}px`;
  }

  function hideContextMenu() {
    refs.contextMenu.hidden = true;
    state.contextText = '';
  }

  function jumpToOrigin(text) {
    const origin = state.originByText.get(text);
    if (!origin) return;
    clearSearch(false);
    selectCategory(origin.category, { preserveSearch: true, target: text });
    forceRenderPendingTarget();
  }

  function forceRenderPendingTarget() {
    if (!state.pendingTarget) return;
    let guard = 0;
    while (state.renderBlockIndex < state.renderBlocks.length && guard++ < 10000) {
      const next = state.renderBlocks[state.renderBlockIndex];
      appendCategoryBlocks(false);
      if (next?.items?.includes(state.pendingTarget) || findSymbolButton(state.pendingTarget)) break;
    }
    requestAnimationFrame(scrollPendingTargetIntoView);
  }

  function scrollPendingTargetIntoView() {
    const text = state.pendingTarget;
    if (!text) return;
    const button = findSymbolButton(text);
    if (!button) return;
    state.pendingTarget = null;
    selectSymbol(text);
    button.scrollIntoView({ block: 'center', behavior: 'smooth' });
    button.focus({ preventScroll: true });
    showToast('已跳到所在位置');
  }

  function findSymbolButton(text) {
    return [...refs.symbolContent.querySelectorAll('.symbol-button')].find(button => button.dataset.symbol === text) || null;
  }

  function handleSymbolPointerOver(event) {
    const button = event.target.closest('.symbol-button');
    if (!button || button.contains(event.relatedTarget)) return;
    const text = button.dataset.symbol;
    updateStatus(text);
    window.clearTimeout(state.tooltipTimer);
    state.tooltipTimer = window.setTimeout(() => showTooltip(button, text), 220);
  }

  function handleSymbolPointerOut(event) {
    const button = event.target.closest('.symbol-button');
    if (!button || button.contains(event.relatedTarget)) return;
    window.clearTimeout(state.tooltipTimer);
    hideTooltip();
  }

  function handleSymbolFocus(event) {
    const button = event.target.closest('.symbol-button');
    if (!button) return;
    const text = button.dataset.symbol;
    updateStatus(text);
    showTooltip(button, text);
  }

  function showTooltip(button, text) {
    const info = getSymbolInfo(text, button.dataset.contextCategory);
    refs.tooltip.replaceChildren(
      tooltipRow('符号：', info.text, 'tooltip-symbol'),
      tooltipRow('中文名称：', info.nameZh),
      tooltipRow('英文名称：', info.nameEn),
      tooltipRow('分类：', info.origin.category),
      tooltipRow('信息：', info.positionText),
      tooltipRow('编码：', info.code)
    );
    refs.tooltip.hidden = false;
    const target = button.getBoundingClientRect();
    const tip = refs.tooltip.getBoundingClientRect();
    let left = target.left + target.width / 2 - tip.width / 2;
    let top = target.bottom + 8;
    left = clamp(left, 8, window.innerWidth - tip.width - 8);
    if (top + tip.height > window.innerHeight - 8) top = target.top - tip.height - 8;
    refs.tooltip.style.left = `${left}px`;
    refs.tooltip.style.top = `${Math.max(8, top)}px`;
  }

  function tooltipRow(label, value, extraClass = '') {
    const row = document.createElement('div');
    row.className = 'tooltip-row';
    const labelEl = document.createElement('span');
    labelEl.className = 'tooltip-label';
    labelEl.textContent = label;
    const valueEl = document.createElement('span');
    valueEl.className = `tooltip-value ${extraClass}`.trim();
    valueEl.textContent = value || '—';
    row.append(labelEl, valueEl);
    return row;
  }

  function hideTooltip() {
    refs.tooltip.hidden = true;
  }

  function getSymbolInfo(text, contextCategory = '') {
    const record = state.symbolByText.get(text);
    const contextual = state.locationByCategory.get(contextCategory)?.get(text);
    const origin = contextual || state.originByText.get(text) || {
      category: contextCategory && !['搜索结果', '最近使用', '常用符号', '自定义'].includes(contextCategory)
        ? contextCategory
        : '自定义',
      group: '', row: 0, item: 0
    };
    const positionParts = [];
    if (origin.group && origin.group !== '_') positionParts.push(origin.group);
    if (origin.row > 0 && origin.item > 0) positionParts.push(`第 ${origin.row} 行，第 ${origin.item} 个`);
    return {
      text,
      nameZh: record?.name_zh || (state.custom.includes(text) ? '自定义符号' : '未命名符号'),
      nameEn: record?.name_en || record?.name || 'CUSTOM SYMBOL',
      emoji: Boolean(record?.emoji) || looksLikeEmoji(text),
      origin,
      positionText: positionParts.join(' · ') || '动态列表',
      code: formatCodePoints(text)
    };
  }

  function updateStatus(text) {
    const info = getSymbolInfo(text);
    refs.statusSymbol.textContent = info.text;
    refs.statusZh.textContent = info.nameZh;
    refs.statusCode.textContent = info.code;
    refs.statusEn.textContent = info.nameEn;
    refs.statusCategory.textContent = info.origin.category;
  }

  function addCustomFromInput() {
    const raw = refs.customInput.value.trim();
    if (!raw) return;
    const values = parseCustomInput(raw);
    let added = 0;
    for (const text of values) {
      if (!text || state.custom.includes(text)) continue;
      state.custom.push(text);
      added++;
    }
    if (!added) {
      showToast('没有新增内容');
      return;
    }
    saveJson(STORAGE.custom, state.custom);
    refs.customInput.value = '';
    renderSidebar();
    renderCurrentCategory();
    showToast(`已添加 ${added} 个自定义符号`);
  }

  function parseCustomInput(raw) {
    const codePointSequence = raw.match(/^(?:U\+[0-9A-Fa-f]{1,6})(?:\s+U\+[0-9A-Fa-f]{1,6})*$/);
    if (codePointSequence) {
      try {
        return [raw.split(/\s+/).map(part => String.fromCodePoint(parseInt(part.slice(2), 16))).join('')];
      } catch {
        return [];
      }
    }
    if (/[，,\n\r]/.test(raw)) {
      return raw.split(/[，,\n\r]+/).map(item => item.trim()).filter(Boolean);
    }
    return [raw];
  }

  function removeCustom(text) {
    const next = state.custom.filter(item => item !== text);
    if (next.length === state.custom.length) return;
    state.custom = next;
    state.common = state.common.filter(item => item !== text);
    state.recent = state.recent.filter(item => item !== text);
    saveJson(STORAGE.custom, state.custom);
    saveJson(STORAGE.common, state.common);
    saveJson(STORAGE.recent, state.recent);
    renderRecent();
    renderSidebar();
    if (state.currentCategory === '自定义' && !state.currentSearch) renderCurrentCategory();
    showToast(`已删除自定义符号：${text}`);
  }

  function updateDataBadge() {
    const count = state.data.symbols.length.toLocaleString('zh-CN');
    const shortHash = state.buildInfo?.source_sha256?.slice(0, 8);
    refs.dataBadge.textContent = shortHash ? `${count}项 · ${shortHash}` : `${count}项`;
    refs.dataBadge.title = state.buildInfo?.source_sha256
      ? `catalog.generated.json SHA-256: ${state.buildInfo.source_sha256}`
      : 'catalog.generated.json 已加载';
  }

  function showToast(message) {
    window.clearTimeout(state.toastTimer);
    refs.toast.textContent = message;
    refs.toast.hidden = false;
    state.toastTimer = window.setTimeout(() => { refs.toast.hidden = true; }, 1600);
  }

  function showFatalError(error) {
    console.error(error);
    refs.app.setAttribute('aria-busy', 'false');
    refs.viewTitle.textContent = '数据加载失败';
    refs.viewMeta.textContent = '请通过 HTTP/HTTPS 服务器访问，不要直接双击 file:// 打开。';
    refs.symbolContent.innerHTML = '';
    const box = document.createElement('div');
    box.className = 'empty-state';
    const title = document.createElement('strong');
    title.textContent = '无法读取符号目录';
    const detail = document.createElement('span');
    detail.textContent = error instanceof Error ? error.message : String(error);
    const hint = document.createElement('span');
    hint.textContent = '本地预览可在项目根目录运行：build.bat web-serve';
    box.append(title, detail, hint);
    refs.symbolContent.append(box);
    refs.dataBadge.textContent = '加载失败';
  }

  function registerServiceWorker() {
    if (!('serviceWorker' in navigator) || location.protocol === 'file:') return;
    navigator.serviceWorker.register('./sw.js').catch(error => console.warn('Service worker registration failed:', error));
  }

  function formatCodePoints(text) {
    return Array.from(text).map(char => `U+${char.codePointAt(0).toString(16).toUpperCase().padStart(4, '0')}`).join(' ');
  }

  function looksLikeEmoji(text) {
    return /[\u{1F000}-\u{1FAFF}\u{2600}-\u{27BF}]/u.test(text) || text.includes('\uFE0F') || text.includes('\u200D');
  }

  function loadJson(key, fallback) {
    try {
      const raw = localStorage.getItem(key);
      return raw === null ? fallback : JSON.parse(raw);
    } catch {
      return fallback;
    }
  }

  function saveJson(key, value) {
    try {
      localStorage.setItem(key, JSON.stringify(value));
    } catch (error) {
      console.warn(`Failed to save ${key}`, error);
    }
  }

  function saveSettings() {
    saveJson(STORAGE.settings, state.settings);
  }

  function uniqueStrings(values) {
    if (!Array.isArray(values)) return [];
    return [...new Set(values.filter(value => typeof value === 'string' && value.length > 0))];
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }
})();
