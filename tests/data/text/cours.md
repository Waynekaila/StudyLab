---
title: Thermodynamique — cours
author: Yunus Çengel; Michael Boles
lang: fr
tags: [cours]
---

# Thermodynamique

Introduction au **premier principe** et au *second*, avec `ΔU = Q + W` et $S$.
Une ligne qui continue
le même paragraphe.  
Après un saut forcé.

## Premier principe

> L'énergie se conserve.
> Elle change de forme.

1. Système fermé
2. Système ouvert
   - régime permanent
   - régime transitoire
3. [ ] À revoir

![Schéma du cycle](img/cycle.png)
![Absente](img/absente.png)

Voir [[Entropie|l'entropie]], [Wikipédia](https://fr.wikipedia.org/wiki/Thermodynamique) et [une note](autre.md).

## Second principe

| Grandeur | Unité | Symbole |
|:---|:---:|---:|
| Entropie | J/K | S |
| Énergie \| travail | J | W |

```python
def rendement(tc, tf):
    return 1 - tf / tc  # <Carnot>
```

$$
\eta = 1 - \frac{T_f}{T_c}
$$

---

Fin du chapitre & conclusion < 1 > 0.

### Détail

Sous-section, *dans* le second principe.
