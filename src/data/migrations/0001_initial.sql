-- StudyLab — migration 0001 : schéma initial
-- La table schema_migrations est gérée par Migrator ; ce fichier est exécuté dans une transaction.
-- Conventions :
--   * id INTEGER PRIMARY KEY (rowid, rapide) + uuid TEXT unique (stable pour export/liens/sync future)
--   * dates en TEXT ISO-8601 UTC ('2026-11-19T15:56:13Z')
--   * suppression douce via deleted_at (corbeille 30 jours)
--   * JSON stocké en TEXT, validé par json_valid()

-- ───────────────────────── Méta ─────────────────────────

CREATE TABLE settings (
  key    TEXT PRIMARY KEY,
  value  TEXT NOT NULL            -- JSON
);

-- ───────────────────────── Bibliothèque ─────────────────────────
CREATE TABLE documents (
  id              INTEGER PRIMARY KEY,
  uuid            TEXT NOT NULL UNIQUE,
  title           TEXT NOT NULL,
  sort_title      TEXT NOT NULL,
  subtitle        TEXT,
  publisher       TEXT,
  year            INTEGER,
  isbn            TEXT,
  doi             TEXT,
  language        TEXT,                       -- BCP-47 : 'fr', 'en'
  description     TEXT,
  discipline      TEXT,
  category        TEXT,
  format          TEXT NOT NULL CHECK (format IN ('pdf','epub','txt','md','html','djvu','docx','image')),
  storage_mode    TEXT NOT NULL DEFAULT 'managed' CHECK (storage_mode IN ('managed','linked')),
  file_path       TEXT NOT NULL,              -- relatif à library/ si managed, absolu si linked
  file_hash       TEXT NOT NULL,              -- SHA-256 hex
  file_size       INTEGER NOT NULL,
  file_mtime      TEXT,
  page_count      INTEGER,                    -- PDF : pages ; EPUB : éléments du spine
  cover_path      TEXT,
  status          TEXT NOT NULL DEFAULT 'to_read'
                  CHECK (status IN ('to_read','reading','paused','finished','to_reread','archived')),
  favorite        INTEGER NOT NULL DEFAULT 0,
  rating          INTEGER CHECK (rating BETWEEN 0 AND 5),
  text_indexed    INTEGER NOT NULL DEFAULT 0, -- 0 non, 1 texte natif, 2 OCR
  added_at        TEXT NOT NULL,
  updated_at      TEXT NOT NULL,
  last_opened_at  TEXT,
  deleted_at      TEXT
);
CREATE UNIQUE INDEX ux_documents_hash ON documents(file_hash) WHERE deleted_at IS NULL; -- doublons
CREATE INDEX ix_documents_status   ON documents(status)        WHERE deleted_at IS NULL;
CREATE INDEX ix_documents_opened   ON documents(last_opened_at DESC);
CREATE INDEX ix_documents_sort     ON documents(sort_title);

CREATE TABLE authors (
  id         INTEGER PRIMARY KEY,
  name       TEXT NOT NULL,
  sort_name  TEXT NOT NULL,
  UNIQUE (name)
);

CREATE TABLE document_authors (
  document_id  INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
  author_id    INTEGER NOT NULL REFERENCES authors(id)   ON DELETE CASCADE,
  role         TEXT NOT NULL DEFAULT 'author',            -- author, editor, translator
  position     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (document_id, author_id, role)
);
CREATE INDEX ix_document_authors_author ON document_authors(author_id);

-- Mots-clés et tags unifiés (un tag peut s'appliquer à documents, notes, annotations, cartes)
CREATE TABLE tags (
  id     INTEGER PRIMARY KEY,
  name   TEXT NOT NULL UNIQUE COLLATE NOCASE,              -- 'thermo/entropie' (hiérarchie par '/')
  color  TEXT
);

CREATE TABLE entity_tags (
  tag_id       INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
  entity_type  TEXT NOT NULL CHECK (entity_type IN ('document','note','annotation','comment','card')),
  entity_id    INTEGER NOT NULL,
  PRIMARY KEY (tag_id, entity_type, entity_id)
);
CREATE INDEX ix_entity_tags_entity ON entity_tags(entity_type, entity_id);

-- Matières (hiérarchiques : Physique > Thermodynamique)
CREATE TABLE subjects (
  id          INTEGER PRIMARY KEY,
  uuid        TEXT NOT NULL UNIQUE,
  parent_id   INTEGER REFERENCES subjects(id) ON DELETE SET NULL,
  name        TEXT NOT NULL,
  description TEXT,
  color       TEXT,                     -- jeton de palette : 'slate','lavender','sage',…
  icon        TEXT,
  position    INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL,
  archived    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE subject_items (
  subject_id   INTEGER NOT NULL REFERENCES subjects(id) ON DELETE CASCADE,
  entity_type  TEXT NOT NULL CHECK (entity_type IN ('document','note','card_deck')),
  entity_id    INTEGER NOT NULL,
  PRIMARY KEY (subject_id, entity_type, entity_id)
);
CREATE INDEX ix_subject_items_entity ON subject_items(entity_type, entity_id);

-- Collections : manuelles, intelligentes, listes de lecture, projets, dossiers virtuels
CREATE TABLE collections (
  id          INTEGER PRIMARY KEY,
  uuid        TEXT NOT NULL UNIQUE,
  parent_id   INTEGER REFERENCES collections(id) ON DELETE SET NULL,
  kind        TEXT NOT NULL CHECK (kind IN ('manual','smart','reading_list','project','folder')),
  name        TEXT NOT NULL,
  description TEXT,
  icon        TEXT,
  color       TEXT,
  query_json  TEXT CHECK (query_json IS NULL OR json_valid(query_json)), -- règles des collections intelligentes
  sort_json   TEXT,
  position    INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL,
  updated_at  TEXT NOT NULL
);

CREATE TABLE collection_items (
  collection_id INTEGER NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
  entity_type   TEXT NOT NULL CHECK (entity_type IN ('document','note')),
  entity_id     INTEGER NOT NULL,
  position      INTEGER NOT NULL DEFAULT 0,   -- ordre dans une liste de lecture
  added_at      TEXT NOT NULL,
  PRIMARY KEY (collection_id, entity_type, entity_id)
);

-- ───────────────────────── Lecture ─────────────────────────
CREATE TABLE reading_progress (
  document_id   INTEGER PRIMARY KEY REFERENCES documents(id) ON DELETE CASCADE,
  locator_json  TEXT NOT NULL CHECK (json_valid(locator_json)),  -- dernière position exacte
  progress      REAL NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 1),
  view_json     TEXT,               -- zoom, mode (1/2 pages), rotation, thème EPUB
  updated_at    TEXT NOT NULL
);

CREATE TABLE reading_sessions (    -- historique + statistiques (temps, pages)
  id           INTEGER PRIMARY KEY,
  document_id  INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
  started_at   TEXT NOT NULL,
  ended_at     TEXT,
  seconds      INTEGER NOT NULL DEFAULT 0,  -- temps actif (fenêtre au premier plan, pas d'inactivité > 2 min)
  pages_seen   TEXT                          -- JSON array d'index de pages vues
);
CREATE INDEX ix_reading_sessions_doc  ON reading_sessions(document_id, started_at);
CREATE INDEX ix_reading_sessions_date ON reading_sessions(started_at);

-- Texte extrait par page (PDF) ou par chapitre (EPUB), natif ou OCR — source de l'index FTS
CREATE TABLE page_text (
  document_id  INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
  unit_index   INTEGER NOT NULL,             -- page (PDF) ou index spine (EPUB)
  source       TEXT NOT NULL CHECK (source IN ('native','ocr')),
  content      TEXT NOT NULL,
  words_json   TEXT,                         -- OCR : boîtes de mots normalisées pour la sélection
  PRIMARY KEY (document_id, unit_index)
);

-- ───────────────────────── Annotations ─────────────────────────
CREATE TABLE annotations (
  id             INTEGER PRIMARY KEY,
  uuid           TEXT NOT NULL UNIQUE,
  document_id    INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
  type           TEXT NOT NULL CHECK (type IN ('highlight','underline','strikeout','note','marker','bookmark')),
  color          TEXT NOT NULL DEFAULT 'yellow' CHECK (color IN ('yellow','green','blue','pink','purple')),
  unit_index     INTEGER NOT NULL,           -- page ou chapitre : tri et filtres rapides
  page_label     TEXT,                       -- numéro imprimé ('xii', '127') si connu
  locator_json   TEXT NOT NULL CHECK (json_valid(locator_json)), -- voir 02-architecture §2.6
  sort_key       REAL NOT NULL,              -- unit_index + position relative : ordre de lecture
  selected_text  TEXT,                       -- passage concerné
  title          TEXT,                       -- titre court optionnel
  anchor_status  TEXT NOT NULL DEFAULT 'ok' CHECK (anchor_status IN ('ok','reanchored','fuzzy','orphan')),
  doc_hash       TEXT NOT NULL,              -- empreinte du fichier au moment de la création
  created_at     TEXT NOT NULL,
  updated_at     TEXT NOT NULL,
  deleted_at     TEXT
);
CREATE INDEX ix_annotations_doc   ON annotations(document_id, sort_key) WHERE deleted_at IS NULL;
CREATE INDEX ix_annotations_date  ON annotations(created_at DESC);
CREATE INDEX ix_annotations_color ON annotations(document_id, color);

-- ───────────────────────── Commentaires ─────────────────────────
CREATE TABLE comments (
  id             INTEGER PRIMARY KEY,
  uuid           TEXT NOT NULL UNIQUE,
  document_id    INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
  annotation_id  INTEGER REFERENCES annotations(id) ON DELETE SET NULL,
  anchor_kind    TEXT NOT NULL CHECK (anchor_kind IN ('free','annotation','passage','page')),
  locator_json   TEXT CHECK (locator_json IS NULL OR json_valid(locator_json)), -- passage/page
  unit_index     INTEGER,
  kind           TEXT NOT NULL DEFAULT 'reflection'
                 CHECK (kind IN ('reflection','question','explanation','reference','summary')),
  body_md        TEXT NOT NULL,
  resolved       INTEGER NOT NULL DEFAULT 0, -- pour les questions
  created_at     TEXT NOT NULL,
  updated_at     TEXT NOT NULL,
  deleted_at     TEXT
);
CREATE INDEX ix_comments_doc ON comments(document_id, unit_index) WHERE deleted_at IS NULL;
CREATE INDEX ix_comments_ann ON comments(annotation_id);

-- ───────────────────────── Notes ─────────────────────────
CREATE TABLE notes (
  id           INTEGER PRIMARY KEY,
  uuid         TEXT NOT NULL UNIQUE,
  title        TEXT NOT NULL,
  slug         TEXT NOT NULL UNIQUE,          -- nom de fichier à l'export, cible des [[liens]]
  note_type    TEXT NOT NULL DEFAULT 'personal'
               CHECK (note_type IN ('reading','course','concept','formula','revision','research','personal','report')),
  body_md      TEXT NOT NULL DEFAULT '',
  frontmatter  TEXT CHECK (frontmatter IS NULL OR json_valid(frontmatter)), -- YAML importé/exporté
  folder       TEXT,                          -- dossier virtuel 'Thermodynamique/Cours'
  pinned       INTEGER NOT NULL DEFAULT 0,
  created_at   TEXT NOT NULL,
  updated_at   TEXT NOT NULL,
  deleted_at   TEXT
);
CREATE INDEX ix_notes_updated ON notes(updated_at DESC);
CREATE INDEX ix_notes_type    ON notes(note_type);

CREATE TABLE note_aliases (           -- alias pour résoudre les [[liens]]
  note_id  INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  alias    TEXT NOT NULL COLLATE NOCASE,
  PRIMARY KEY (note_id, alias)
);

CREATE TABLE attachments (
  id          INTEGER PRIMARY KEY,
  uuid        TEXT NOT NULL UNIQUE,
  note_id     INTEGER REFERENCES notes(id) ON DELETE SET NULL,
  file_name   TEXT NOT NULL,
  rel_path    TEXT NOT NULL,                  -- dans attachments/
  mime        TEXT,
  created_at  TEXT NOT NULL
);

-- ───────────────────────── Relations (graphe) ─────────────────────────
-- Table unique d'arêtes : source → cible, typée. Les rétroliens = requête inverse.
CREATE TABLE links (
  id           INTEGER PRIMARY KEY,
  source_type  TEXT NOT NULL CHECK (source_type IN ('note','document','annotation','comment','card','subject','concept')),
  source_id    INTEGER NOT NULL,
  target_type  TEXT NOT NULL CHECK (target_type IN ('note','document','annotation','comment','card','subject','concept')),
  target_id    INTEGER,                       -- NULL = lien non résolu (note pas encore créée)
  target_text  TEXT,                          -- texte du [[lien]] pour résolution ultérieure
  kind         TEXT NOT NULL DEFAULT 'reference'
               CHECK (kind IN ('reference','derived_from','cites','related','prerequisite','defines')),
  origin       TEXT NOT NULL DEFAULT 'manual' CHECK (origin IN ('manual','wikilink','derived')),
  created_at   TEXT NOT NULL
);
CREATE INDEX ix_links_source ON links(source_type, source_id);
CREATE INDEX ix_links_target ON links(target_type, target_id);
CREATE INDEX ix_links_unresolved ON links(target_text) WHERE target_id IS NULL;

CREATE TABLE concepts (               -- nœuds conceptuels légers (#tag promu ou concept nommé)
  id          INTEGER PRIMARY KEY,
  uuid        TEXT NOT NULL UNIQUE,
  name        TEXT NOT NULL UNIQUE COLLATE NOCASE,
  definition  TEXT,
  created_at  TEXT NOT NULL
);

-- ───────────────────────── Références bibliographiques ─────────────────────────
CREATE TABLE references_ (            -- « references » est un mot réservé SQL
  id           INTEGER PRIMARY KEY,
  document_id  INTEGER REFERENCES documents(id) ON DELETE SET NULL,
  citekey      TEXT NOT NULL UNIQUE,  -- 'cengel2019thermo'
  csl_json     TEXT NOT NULL CHECK (json_valid(csl_json)), -- format CSL-JSON
  created_at   TEXT NOT NULL
);

-- ───────────────────────── Révision ─────────────────────────
CREATE TABLE decks (
  id          INTEGER PRIMARY KEY,
  uuid        TEXT NOT NULL UNIQUE,
  name        TEXT NOT NULL,
  subject_id  INTEGER REFERENCES subjects(id) ON DELETE SET NULL,
  scheduler   TEXT NOT NULL DEFAULT 'sm2' CHECK (scheduler IN ('sm2','fsrs')),
  config_json TEXT,                           -- nouvelles cartes/jour, rétention visée…
  created_at  TEXT NOT NULL
);

CREATE TABLE cards (
  id           INTEGER PRIMARY KEY,
  uuid         TEXT NOT NULL UNIQUE,
  deck_id      INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,
  card_type    TEXT NOT NULL DEFAULT 'basic' CHECK (card_type IN ('basic','reversed','cloze','formula')),
  front_md     TEXT NOT NULL,
  back_md      TEXT NOT NULL,
  source_type  TEXT CHECK (source_type IN ('annotation','note','comment','document')),
  source_id    INTEGER,
  suspended    INTEGER NOT NULL DEFAULT 0,
  created_at   TEXT NOT NULL,
  updated_at   TEXT NOT NULL,
  deleted_at   TEXT
);
CREATE INDEX ix_cards_deck ON cards(deck_id);

CREATE TABLE card_state (             -- état courant du planificateur (1 ligne par carte)
  card_id      INTEGER PRIMARY KEY REFERENCES cards(id) ON DELETE CASCADE,
  state        TEXT NOT NULL DEFAULT 'new' CHECK (state IN ('new','learning','review','relearning')),
  due_at       TEXT,
  interval_d   REAL NOT NULL DEFAULT 0,
  ease         REAL NOT NULL DEFAULT 2.5,      -- SM-2
  stability    REAL,                           -- FSRS
  difficulty   REAL,                           -- FSRS
  reps         INTEGER NOT NULL DEFAULT 0,
  lapses       INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX ix_card_state_due ON card_state(due_at);

CREATE TABLE card_reviews (           -- journal complet, jamais modifié
  id           INTEGER PRIMARY KEY,
  card_id      INTEGER NOT NULL REFERENCES cards(id) ON DELETE CASCADE,
  reviewed_at  TEXT NOT NULL,
  rating       INTEGER NOT NULL CHECK (rating BETWEEN 1 AND 4), -- 1 À revoir, 2 Difficile, 3 Bien, 4 Facile
  elapsed_ms   INTEGER,
  prev_state_json TEXT,
  new_state_json  TEXT
);
CREATE INDEX ix_card_reviews_card ON card_reviews(card_id, reviewed_at);

-- ───────────────────────── Activité & objectifs ─────────────────────────
CREATE TABLE activity (
  id           INTEGER PRIMARY KEY,
  at           TEXT NOT NULL,
  action       TEXT NOT NULL,                 -- 'opened','annotated','commented','note_created','reviewed','imported'
  entity_type  TEXT NOT NULL,
  entity_id    INTEGER NOT NULL,
  detail_json  TEXT
);
CREATE INDEX ix_activity_at ON activity(at DESC);

CREATE TABLE goals (
  id          INTEGER PRIMARY KEY,
  kind        TEXT NOT NULL CHECK (kind IN ('pages_per_week','minutes_per_day','documents_finished','reviews_per_day')),
  target      INTEGER NOT NULL,
  subject_id  INTEGER REFERENCES subjects(id) ON DELETE CASCADE,
  active      INTEGER NOT NULL DEFAULT 1
);

-- ───────────────────────── Recherche plein texte (FTS5) ─────────────────────────
-- Tables « external content » : pas de duplication du texte, synchronisées par triggers.
CREATE VIRTUAL TABLE fts_documents USING fts5(
  title, authors, publisher, description, keywords, isbn, doi,
  content='', tokenize='unicode61 remove_diacritics 2', prefix='2 3'
);  -- contentless : alimentée par LibraryService (agrège auteurs + tags), rowid = documents.id

CREATE VIRTUAL TABLE fts_page_text USING fts5(
  content, content='page_text', content_rowid='rowid',
  tokenize='unicode61 remove_diacritics 2', prefix='3'
);

CREATE VIRTUAL TABLE fts_annotations USING fts5(
  selected_text, title, content='annotations', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2', prefix='2 3'
);

CREATE VIRTUAL TABLE fts_comments USING fts5(
  body_md, content='comments', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2', prefix='2 3'
);

CREATE VIRTUAL TABLE fts_notes USING fts5(
  title, body_md, content='notes', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2', prefix='2 3'
);

-- Triggers de synchronisation (exemple pour notes ; même schéma pour annotations, comments, page_text)
CREATE TRIGGER trg_notes_ai AFTER INSERT ON notes BEGIN
  INSERT INTO fts_notes(rowid, title, body_md) VALUES (new.id, new.title, new.body_md);
END;
CREATE TRIGGER trg_notes_ad AFTER DELETE ON notes BEGIN
  INSERT INTO fts_notes(fts_notes, rowid, title, body_md) VALUES ('delete', old.id, old.title, old.body_md);
END;
CREATE TRIGGER trg_notes_au AFTER UPDATE OF title, body_md ON notes BEGIN
  INSERT INTO fts_notes(fts_notes, rowid, title, body_md) VALUES ('delete', old.id, old.title, old.body_md);
  INSERT INTO fts_notes(rowid, title, body_md) VALUES (new.id, new.title, new.body_md);
END;

CREATE TRIGGER trg_ann_ai AFTER INSERT ON annotations BEGIN
  INSERT INTO fts_annotations(rowid, selected_text, title) VALUES (new.id, new.selected_text, new.title);
END;
CREATE TRIGGER trg_ann_ad AFTER DELETE ON annotations BEGIN
  INSERT INTO fts_annotations(fts_annotations, rowid, selected_text, title) VALUES ('delete', old.id, old.selected_text, old.title);
END;
CREATE TRIGGER trg_ann_au AFTER UPDATE OF selected_text, title ON annotations BEGIN
  INSERT INTO fts_annotations(fts_annotations, rowid, selected_text, title) VALUES ('delete', old.id, old.selected_text, old.title);
  INSERT INTO fts_annotations(rowid, selected_text, title) VALUES (new.id, new.selected_text, new.title);
END;

CREATE TRIGGER trg_com_ai AFTER INSERT ON comments BEGIN
  INSERT INTO fts_comments(rowid, body_md) VALUES (new.id, new.body_md);
END;
CREATE TRIGGER trg_com_ad AFTER DELETE ON comments BEGIN
  INSERT INTO fts_comments(fts_comments, rowid, body_md) VALUES ('delete', old.id, old.body_md);
END;
CREATE TRIGGER trg_com_au AFTER UPDATE OF body_md ON comments BEGIN
  INSERT INTO fts_comments(fts_comments, rowid, body_md) VALUES ('delete', old.id, old.body_md);
  INSERT INTO fts_comments(rowid, body_md) VALUES (new.id, new.body_md);
END;

CREATE TRIGGER trg_pt_ai AFTER INSERT ON page_text BEGIN
  INSERT INTO fts_page_text(rowid, content) VALUES (new.rowid, new.content);
END;
CREATE TRIGGER trg_pt_ad AFTER DELETE ON page_text BEGIN
  INSERT INTO fts_page_text(fts_page_text, rowid, content) VALUES ('delete', old.rowid, old.content);
END;
CREATE TRIGGER trg_pt_au AFTER UPDATE OF content ON page_text BEGIN
  INSERT INTO fts_page_text(fts_page_text, rowid, content) VALUES ('delete', old.rowid, old.content);
  INSERT INTO fts_page_text(rowid, content) VALUES (new.rowid, new.content);
END;
