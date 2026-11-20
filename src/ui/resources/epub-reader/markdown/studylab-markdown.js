// Rendu Markdown de StudyLab : markdown-it (HTML brut désactivé) + KaTeX pour les formules ($…$, $$…$$).
// Trois modes : liste de cartes (volet Commentaires), aperçu d'un texte (éditeur de commentaire) et
// note (espace de notes : [[liens]] cliquables, liens non résolus signalés, images jointes, titres repérés
// par leur ligne pour le plan).
'use strict'

let bridge = null
const post = (event, data) => { if (bridge) bridge.post(event, JSON.stringify(data ?? {})) }

// ── Formules ────────────────────────────────────────────────────────────────
function renderMath (tex, display) {
  try {
    return katex.renderToString(tex, { displayMode: display, throwOnError: false, output: 'html', strict: false })
  } catch (e) {
    const span = document.createElement('span')
    span.className = 'math-error'
    span.textContent = tex
    return span.outerHTML
  }
}

function mathPlugin (md) {
  // $…$ : pas d'espace juste après l'ouverture ni juste avant la fermeture, pas de chiffre après (« 5 $ »).
  md.inline.ruler.after('escape', 'math_inline', (state, silent) => {
    const src = state.src
    let pos = state.pos
    if (src[pos] !== '$') return false
    const display = src[pos + 1] === '$'
    const open = display ? 2 : 1
    const start = pos + open
    if (start >= state.posMax || /\s/.test(src[start])) return false
    let end = start
    while ((end = src.indexOf(display ? '$$' : '$', end)) !== -1) {
      if (src[end - 1] === '\\') { end += 1; continue }
      break
    }
    if (end === -1 || end === start) return false
    if (!display && (/\s/.test(src[end - 1]) || /\d/.test(src[end + 1] || ''))) return false
    if (!silent) {
      const token = state.push('math_inline', 'math', 0)
      token.content = src.slice(start, end)
      token.markup = display ? '$$' : '$'
    }
    state.pos = end + open
    return true
  })
  // $$ … $$ sur ses propres lignes.
  md.block.ruler.after('blockquote', 'math_block', (state, startLine, endLine, silent) => {
    let pos = state.bMarks[startLine] + state.tShift[startLine]
    let max = state.eMarks[startLine]
    if (pos + 2 > max || state.src.slice(pos, pos + 2) !== '$$') return false
    let first = state.src.slice(pos + 2, max)
    let content
    let line = startLine
    if (first.trim().endsWith('$$') && first.trim().length > 2) {
      content = first.trim().slice(0, -2)
    } else {
      const lines = [first]
      let found = false
      while (++line < endLine) {
        pos = state.bMarks[line] + state.tShift[line]
        max = state.eMarks[line]
        const text = state.src.slice(pos, max)
        if (text.trim().endsWith('$$')) { lines.push(text.trim().slice(0, -2)); found = true; break }
        lines.push(text)
      }
      if (!found) return false
      content = lines.join('\n')
    }
    if (silent) return true
    const token = state.push('math_block', 'math', 0)
    token.block = true
    token.content = content
    token.map = [startLine, line + 1]
    state.line = line + 1
    return true
  }, { alt: ['paragraph', 'reference', 'blockquote', 'list'] })
  md.renderer.rules.math_inline = (tokens, idx) => renderMath(tokens[idx].content, tokens[idx].markup === '$$')
  md.renderer.rules.math_block = (tokens, idx) => renderMath(tokens[idx].content, true) + '\n'
}

function wikiLinkPlugin (md) {
  md.inline.ruler.before('link', 'wikilink', (state, silent) => {
    const src = state.src
    if (!src.startsWith('[[', state.pos)) return false
    const end = src.indexOf(']]', state.pos + 2)
    if (end === -1) return false
    const inner = src.slice(state.pos + 2, end)
    if (!inner.trim() || /[\[\]\n]/.test(inner)) return false
    if (!silent) {
      const bar = inner.indexOf('|')
      const token = state.push('wikilink', 'span', 0)
      token.meta = { target: (bar >= 0 ? inner.slice(0, bar) : inner).trim(), label: (bar >= 0 ? inner.slice(bar + 1) : inner).trim(), raw: inner }
    }
    state.pos = end + 2
    return true
  })
  md.renderer.rules.wikilink = (tokens, idx) => {
    const m = tokens[idx].meta
    const esc = md.utils.escapeHtml
    if (noteMode && m.target.startsWith('@')) {
      // Citation [[@clé]], [[@clé#p. 12]], [[@clé|texte]] : auteur-date, entrée complète en info-bulle.
      const hash = m.target.indexOf('#')
      const key = (hash >= 0 ? m.target.slice(1, hash) : m.target.slice(1)).trim()
      const locator = hash >= 0 ? m.target.slice(hash + 1).trim() : ''
      const c = noteState.citations[key.toLowerCase()]
      const custom = m.raw.includes('|') ? m.label : ''
      if (!c) return `<a class="wikilink citation unresolved" href="#" title="${esc('@' + key + ' · ' + noteState.citationMissingLabel)}" data-target="${esc(m.target)}">${esc(custom || '@' + key)}</a>`
      const text = custom || `(${c.label}${locator ? ', ' + locator : ''})`
      return `<a class="wikilink citation" href="#" title="${esc(c.entry)}" data-target="${esc(m.target)}">${esc(text)}</a>`
    }
    if (noteMode && !m.target.startsWith('@')) {
      const unresolved = noteState.unresolved.includes(m.target)
      const doc = !unresolved && noteState.documents.includes(m.target)
      const title = unresolved ? `${m.target} · ${noteState.unresolvedLabel}` : doc ? `${m.target} · ${noteState.documentLabel}` : m.target
      return `<a class="wikilink${unresolved ? ' unresolved' : ''}${doc ? ' document' : ''}" href="#" title="${esc(title)}" data-target="${esc(m.target)}">${esc(m.label || m.target)}</a>`
    }
    return `<span class="wikilink" title="${esc(m.target)}" data-raw="[[${esc(m.raw)}]]">${esc(m.label || m.target)}</span>`
  }
}

// Mode note : état fourni par l'application à chaque rendu.
let noteMode = false
let noteState = { unresolved: [], documents: [], images: {}, citations: {}, bibliography: [], lineOffset: 0, unresolvedLabel: '', documentLabel: '', citationMissingLabel: '', bibliographyTitle: '' }

const md = window.markdownit({ html: false, linkify: true, breaks: true, typographer: false })
  .use(mathPlugin).use(wikiLinkPlugin)
// Liens : jamais suivis dans la page ; l'application décide (navigateur externe pour http/https).
// Images : seulement les pièces jointes de la note, servies en data: par l'application (hors ligne).
// « studylab://open?… » : passage d'un document (citations des notes de lecture), ouvert dans le lecteur.
md.validateLink = url => /^(https?:|mailto:|studylab:\/\/open[/?])/i.test(url.trim()) || /^attachments\/[\w.-]+$/.test(url.trim())
const defaultImage = md.renderer.rules.image
md.renderer.rules.image = (tokens, idx, options, env, self) => {
  const token = tokens[idx]
  const src = token.attrGet('src') || ''
  const data = noteState.images[src]
  if (!data) {
    const alt = self.renderInlineAsText(token.children, options, env)
    return `<span class="missing-image" title="${md.utils.escapeHtml(src)}">${md.utils.escapeHtml(alt || src)}</span>`
  }
  token.attrSet('src', data)
  return defaultImage(tokens, idx, options, env, self)
}
// Titres repérés par leur ligne dans le texte complet (plan → défilement de l'aperçu).
md.renderer.rules.heading_open = (tokens, idx, options, env, self) => {
  const t = tokens[idx]
  if (t.map) t.attrSet('data-line', String(t.map[0] + (noteState.lineOffset || 0)))
  return self.renderToken(tokens, idx, options)
}

const render = text => md.render(text || '')

// ── Cartes ──────────────────────────────────────────────────────────────────
const ICONS = {
  question: '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/><path d="M9.5 9a2.5 2.5 0 0 1 4.9.7c0 1.6-2.4 2.3-2.4 2.3"/><path d="M12 14.5h.01"/>',
  link: '<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',
  comment: '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>'
}
const svg = name => `<svg viewBox="0 0 24 24" aria-hidden="true">${ICONS[name] || ICONS.comment}</svg>`
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c])

let cards = []
let activeId = 0

function cardHtml (c) {
  const classes = ['card', `kind-${c.kind}`]
  if (c.id === activeId) classes.push('active')
  if (c.resolved) classes.push('resolved')
  const icon = c.anchor === 'annotation' ? 'link' : (c.kind === 'question' ? 'question' : 'comment')
  const parts = [`<span class="label">${esc(c.title)}</span>`]
  if (c.where) parts.push(`<span class="where">· ${esc(c.where)}</span>`)
  const badge = c.kind === 'question' ? `<span class="badge">${esc(c.resolved ? c.resolvedLabel : c.openLabel)}</span>` : ''
  const quote = c.quote ? `<div class="quote" style="--quote:${esc(c.quoteColor || '')}">${esc(c.quote)}</div>` : ''
  return `<article class="${classes.join(' ')}" data-id="${c.id}" tabindex="0" aria-label="${esc(c.title)}">` +
    `<header>${svg(icon)}${parts.join(' ')}${badge}<span class="date">${esc(c.date)}</span>` +
    `<button class="more" title="${esc(c.menuLabel)}" aria-label="${esc(c.menuLabel)}">⋯</button></header>` +
    `${quote}<div class="md">${render(c.body)}</div></article>`
}

function renderCards () {
  const root = document.getElementById('root')
  document.body.classList.remove('preview')
  if (!cards.length) {
    root.innerHTML = `<div class="empty">${esc(window.__emptyText || '')}</div>`
    return
  }
  root.innerHTML = cards.map(cardHtml).join('')
}

function cardOf (target) { return target && target.closest ? target.closest('.card') : null }

document.addEventListener('click', e => {
  const wiki = e.target.closest && e.target.closest('a.wikilink')
  if (wiki) { e.preventDefault(); post('wikilink', { target: wiki.dataset.target }); return }
  const a = e.target.closest && e.target.closest('a[href]')
  if (a) { e.preventDefault(); post('link', { href: a.getAttribute('href') }); return }
  const card = cardOf(e.target)
  if (!card) return
  const id = Number(card.dataset.id)
  if (e.target.closest('.more')) {
    const r = e.target.closest('.more').getBoundingClientRect()
    post('menu', { id, x: r.left, y: r.bottom })
    return
  }
  post('activate', { id })
})
document.addEventListener('dblclick', e => {
  const card = cardOf(e.target)
  if (card) post('edit', { id: Number(card.dataset.id) })
})
document.addEventListener('contextmenu', e => {
  e.preventDefault()
  const card = cardOf(e.target)
  if (card) post('menu', { id: Number(card.dataset.id), x: e.clientX, y: e.clientY })
})
document.addEventListener('keydown', e => {
  const card = cardOf(document.activeElement)
  if (!card) return
  const id = Number(card.dataset.id)
  if (e.key === 'Enter') { e.preventDefault(); post('activate', { id }) } else if (e.key === 'F2') { e.preventDefault(); post('edit', { id }) } else if (e.key === 'Delete') { e.preventDefault(); post('delete', { id }) } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault()
    const next = e.key === 'ArrowDown' ? card.nextElementSibling : card.previousElementSibling
    if (next) next.focus()
  }
})

// ── API appelée par l'application ───────────────────────────────────────────
window.StudyLab = {
  setTheme (vars) {
    for (const [k, v] of Object.entries(vars)) document.documentElement.style.setProperty(`--${k}`, v)
  },
  setCards (list, emptyText) {
    cards = list
    window.__emptyText = emptyText
    renderCards()
  },
  setActive (id, scroll) {
    activeId = id
    for (const el of document.querySelectorAll('.card')) el.classList.toggle('active', Number(el.dataset.id) === id)
    const el = document.querySelector(`.card[data-id="${id}"]`)
    if (el && scroll) el.scrollIntoView({ block: 'nearest', behavior: 'smooth' })
  },
  setMarkdown (text, emptyText) {
    noteMode = false
    document.body.classList.add('preview')
    const root = document.getElementById('root')
    root.innerHTML = text && text.trim() ? `<div class="md">${render(text)}</div>` : `<div class="empty">${esc(emptyText || '')}</div>`
  },
  // note : {title, body (sans front matter), lineOffset, chips: [texte], unresolved: [cible], images: {chemin: data:}}
  setNote (note, emptyText) {
    noteMode = true
    noteState = Object.assign({ unresolved: [], documents: [], images: {}, citations: {}, bibliography: [], lineOffset: 0, unresolvedLabel: '', documentLabel: '', citationMissingLabel: '', bibliographyTitle: '' }, note)
    document.body.classList.add('preview', 'note')
    const root = document.getElementById('root')
    const chips = (note.chips || []).map(c => `<span class="chip">${esc(c)}</span>`).join('')
    const head = (chips ? `<div class="chips">${chips}</div>` : '') + `<h1 class="note-title">${esc(note.title)}</h1>`
    let body = note.body && note.body.trim() ? render(note.body) : `<p class="empty-note">${esc(emptyText || '')}</p>`
    // Bibliographie des références citées, dans l'ordre alphabétique (entrées déjà mises en forme par l'application).
    if (noteState.bibliography.length) body += `<section class="bibliography"><h2>${esc(noteState.bibliographyTitle)}</h2><ul>${noteState.bibliography.map(e => `<li>${e}</li>`).join('')}</ul></section>`
    const y = window.scrollY
    root.innerHTML = `<article class="md note">${head}${body}</article>`
    window.scrollTo(0, y)
  },
  scrollToLine (line) {
    let target = null
    for (const el of document.querySelectorAll('[data-line]')) {
      if (Number(el.dataset.line) <= line) target = el
      else break
    }
    if (target) target.scrollIntoView({ block: 'start', behavior: 'smooth' })
    else window.scrollTo({ top: 0, behavior: 'smooth' })
    return !!target
  },
  wikiLinks: () => [...document.querySelectorAll('a.wikilink')].map(a => ({ target: a.dataset.target, text: a.textContent, unresolved: a.classList.contains('unresolved'), document: a.classList.contains('document'), citation: a.classList.contains('citation') })),
  bibliography: () => [...document.querySelectorAll('.bibliography li')].map(li => li.textContent),
  appLinks: () => [...document.querySelectorAll('a[href^="studylab:"]')].map(a => a.getAttribute('href')),
  clickHref: href => { const a = [...document.querySelectorAll('a[href]')].find(el => el.getAttribute('href') === href); if (a) a.click(); return !!a },
  clickWiki: target => { const a = [...document.querySelectorAll('a.wikilink')].find(el => el.dataset.target === target); if (a) a.click(); return !!a },
  // Tests et diagnostic
  render,
  cardIds: () => [...document.querySelectorAll('.card')].map(el => Number(el.dataset.id)),
  text: () => document.getElementById('root').innerText,
  html: () => document.getElementById('root').innerHTML,
  click: id => { const el = document.querySelector(`.card[data-id="${id}"]`); if (el) el.click(); return !!el },
  contentHeight: () => document.documentElement.scrollHeight
}

new QWebChannel(qt.webChannelTransport, channel => {
  bridge = channel.objects.bridge
  post('ready')
})
