-- Itération 2 : la table fts_documents devient « contentless_delete » (SQLite ≥ 3.43)
-- pour permettre DELETE/UPDATE par rowid lors de la modification ou du retrait d'un document.
-- Aucune donnée n'y était encore écrite (l'itération 1 ne l'alimentait pas).
DROP TABLE fts_documents;
CREATE VIRTUAL TABLE fts_documents USING fts5(
  title, authors, publisher, description, keywords, isbn, doi,
  content='', contentless_delete=1, tokenize='unicode61 remove_diacritics 2', prefix='2 3'
);
