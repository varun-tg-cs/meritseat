const $ = selector => document.querySelector(selector);
let state = { departments: [], applicants: [] };
let toastTimer;

function element(tag, className = '', value) {
  const el = document.createElement(tag);
  el.className = className;
  if (value !== undefined) el.textContent = String(value);
  return el;
}

function toast(message, error = false) {
  const box = $('#toast');
  box.textContent = message;
  box.className = `visible${error ? ' error' : ''}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { box.className = ''; }, 4000);
}

async function refresh() {
  const response = await fetch('/api/state');
  if (!response.ok) throw new Error('Could not load admission records');
  state = await response.json();
  render();
}

async function action(endpoint, fields, successMessage) {
  try {
    const response = await fetch(`/api/${endpoint}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams(fields)
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Request failed');
    await refresh();
    toast(successMessage);
    return true;
  } catch (error) { toast(error.message, true); return false; }
}

function fillOptions(select) {
  const selected = select.value;
  select.replaceChildren(...state.departments.map(dep => {
    const option = element('option', '', dep.name);
    option.value = dep.id;
    return option;
  }));
  if (state.departments.some(dep => String(dep.id) === selected)) select.value = selected;
}

function currentDepartment() {
  return state.departments.find(dep => String(dep.id) === $('#queue-department').value) || state.departments[0];
}

function metric(value, label) {
  const block = element('div', 'seat-metric');
  block.append(element('strong', '', value), element('span', '', label));
  return block;
}

function renderHeap(dep, lookup) {
  const view = $('#heap-view');
  view.replaceChildren();
  if (!dep.heap.length) { view.append(element('p', 'queue-empty', 'The heap is empty.')); return; }
  for (let start = 0, size = 1; start < dep.heap.length; start += size, size *= 2) {
    const level = element('div', 'heap-level');
    dep.heap.slice(start, start + size).forEach(id => {
      const a = lookup.get(id);
      const item = element('div', `heap-node${start === 0 ? ' heap-root' : ''}`);
      item.append(element('strong', '', a?.name || `#${id}`), element('small', '', `${a?.score ?? 0} entrance · ${a?.board ?? 0}% Class 12 · ${a?.tenth < 0 ? 'Class 10 missing' : `${a?.tenth ?? 0}% Class 10`}`));
      level.append(item);
    });
    view.append(level);
  }
}

function renderQueue() {
  const dep = currentDepartment();
  if (!dep) return;
  const vacancies = dep.capacity - dep.admitted;
  const list = $('#queue-list');
  list.replaceChildren();
  $('#seat-summary').replaceChildren(metric(dep.capacity, 'Total seats'), metric(dep.admitted, 'Filled'), metric(vacancies, 'Vacant'));
  $('#queue-count').textContent = `${dep.ranking.length} waiting`;
  const lookup = new Map(state.applicants.map(a => [a.id, a]));
  if (!dep.ranking.length) {
    list.append(element('p', 'queue-empty', 'No one is waiting in this department yet. Add an applicant to see who comes first.'));
  } else {
    dep.ranking.forEach((id, index) => {
      const a = lookup.get(id);
      if (!a) return;
      const row = element('div', `queue-row${index < vacancies ? ' next-seat' : ''}`);
      row.append(element('span', 'rank', String(index + 1).padStart(2, '0')));
      const identity = element('div', 'queue-person');
      identity.append(element('strong', '', a.name), element('small', '', `ID ${a.id} · Class 12: ${a.board}% · Class 10: ${a.tenth < 0 ? 'missing' : `${a.tenth}%`}`));
      row.append(identity, element('strong', 'score', `${a.score}/100`));
      if (index < vacancies) row.append(element('span', 'next-tag', 'Gets a seat'));
      list.append(row);
    });
  }
  const allocatable = Math.min(vacancies, dep.ranking.length);
  $('#allocate-button').disabled = allocatable === 0;
  $('#allocate-label').textContent = allocatable ? `Allocate ${allocatable} ${allocatable === 1 ? 'seat' : 'seats'}` : 'No seats to allocate';
  $('#capacity-input').value = dep.capacity;
  renderHeap(dep, lookup);
}

function renderRecords() {
  const term = $('#search').value.toLowerCase().trim();
  const rows = $('#applicant-rows');
  rows.replaceChildren();
  const found = state.applicants.filter(a => `${a.id} ${a.name}`.toLowerCase().includes(term));
  if (!found.length) {
    const row = element('tr');
    const cell = element('td', 'empty-records', term ? 'No matching applicants.' : 'No applications yet. Add a student or try sample applicants.');
    cell.colSpan = 8; row.append(cell); rows.append(row); return;
  }
  found.slice().reverse().forEach(a => {
    const row = element('tr');
    [a.id, a.name, state.departments[a.department]?.name || '—', `${a.score}/100`, `${a.board}%`, a.tenth < 0 ? 'Missing' : `${a.tenth}%`].forEach((value, i) => row.append(element('td', i === 1 ? 'name-cell' : '', value)));
    const status = element('td'); status.append(element('span', `status ${a.status.toLowerCase()}`, a.status)); row.append(status);
    const actionCell = element('td');
    if (a.tenth < 0) {
      const addMark = element('button', 'cancel-button', 'Add Class 10 %');
      addMark.type = 'button';
      addMark.addEventListener('click', () => {
        const value = prompt(`Enter ${a.name}'s Class 10 percentage (0–100):`);
        if (value === null) return;
        const mark = Number(value);
        if (!/^\d{1,3}$/.test(value.trim()) || !Number.isInteger(mark) || mark < 0 || mark > 100) {
          toast('Enter a whole number from 0 to 100.', true); return;
        }
        action('tenth', { id: a.id, tenth: mark }, 'Class 10 mark saved. The waiting order has been updated.');
      });
      actionCell.append(addMark);
    }
    if (a.status === 'Admitted') {
      const cancel = element('button', 'cancel-button', 'Cancel seat');
      cancel.type = 'button';
      cancel.addEventListener('click', async () => {
        if (!confirm(`Cancel ${a.name}'s seat? The next waiting student in ${state.departments[a.department].name} will be promoted.`)) return;
        const before = new Set(state.applicants.filter(x => x.status === 'Admitted').map(x => x.id));
        if (await action('cancel', { id: a.id }, 'Seat cancelled. The next waiting student was promoted if available.')) {
          const promoted = state.applicants.find(x => x.status === 'Admitted' && !before.has(x.id));
          if (promoted) toast(`${promoted.name} was promoted from the waiting list.`);
        }
      });
      actionCell.append(cancel);
    }
    if (!actionCell.childNodes.length) actionCell.textContent = '—';
    row.append(actionCell); rows.append(row);
  });
}

function render() {
  const admitted = state.applicants.filter(a => a.status === 'Admitted').length;
  const waiting = state.applicants.filter(a => a.status === 'Waiting').length;
  $('#summary').textContent = `${state.applicants.length} applications · ${admitted} admitted · ${waiting} waiting`;
  $('#demo-button').hidden = state.applicants.length !== 0;
  fillOptions($('#add-department'));
  fillOptions($('#queue-department'));
  renderQueue(); renderRecords();
}

$('#add-form').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const details = Object.fromEntries(new FormData(form));
  if (['score', 'board', 'tenth'].some(key => !/^\d{1,3}$/.test(details[key]) || Number(details[key]) > 100)) {
    toast('Enter whole number marks from 0 to 100.', true); return;
  }
  if (await action('applicants', details, `${details.name} joined the waiting queue.`)) {
    form.querySelector('[name="name"]').value = '';
    form.querySelector('[name="score"]').value = '';
    form.querySelector('[name="board"]').value = '';
    form.querySelector('[name="tenth"]').value = '';
    $('#queue-department').value = details.department;
    renderQueue();
  }
});
$('#queue-department').addEventListener('change', renderQueue);
$('#search').addEventListener('input', renderRecords);
$('#demo-button').addEventListener('click', () => action('demo', {}, 'Sample applicants added. Review Computer Science to see the merit order.'));
$('#allocate-button').addEventListener('click', () => {
  const dep = currentDepartment();
  const seats = Math.min(dep.capacity - dep.admitted, dep.ranking.length);
  if (seats > 0) action('allocate', { department: dep.id }, `${seats} ${seats === 1 ? 'student received a seat' : 'students received seats'} in ${dep.name}.`);
});
$('#capacity-button').addEventListener('click', () => {
  const dep = currentDepartment();
  const value = $('#capacity-input').value;
  if (!/^\d{1,3}$/.test(value) || Number(value) > 512 || Number(value) < dep.admitted) {
    toast(`Enter a whole number from ${dep.admitted} to 512.`, true); return;
  }
  action('capacity', { department: dep.id, capacity: value }, 'Seat count saved. Run allocation to fill vacancies.');
});
refresh().catch(error => toast(error.message, true));
