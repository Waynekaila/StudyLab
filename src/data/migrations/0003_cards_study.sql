-- V2-1 : cartes mémoire et répétition espacée.
-- « ordinal » : numéro du trou d'une carte à trous ({{c2::…}} → 2) ; 0 pour les autres types.
ALTER TABLE cards ADD COLUMN ordinal INTEGER NOT NULL DEFAULT 0;
-- Étape d'apprentissage en cours (cartes « learning » / « relearning »).
ALTER TABLE card_state ADD COLUMN step INTEGER NOT NULL DEFAULT 0;
-- Statistiques du jour et des derniers jours.
CREATE INDEX ix_card_reviews_at ON card_reviews(reviewed_at);
