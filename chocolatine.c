/*
 * Chocolatine - projet commencé en Aout 2025
   Merci à OrangeDuck pour les libarary mpc
 */

#include "mpc.h"
#include <math.h>

#ifdef _WIN32

static char buffer[2048];

/* readline rudimentaire pour Windows (pas d'editline) */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* add_history n'existe pas sur cette plateforme, stub */
void add_history(char* unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

/* Déclarations anticipées */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Type de fonction builtin : prend l'environnement et une liste d'arguments */
typedef lval*(*lbuiltin)(lenv*, lval*);

/* Énumération des types de valeurs Lisp */
enum {
  LVAL_ERR,    /* Erreur */
  LVAL_NUM,    /* Nombre */
  LVAL_SYM,    /* Symbole */
  LVAL_FUN,    /* Fonction */
  LVAL_SEXPR,  /* S-expression (evaluated list) */
  LVAL_QEXPR   /* Q-expression (quoted list) */
};

/* ===== DÉFINITION COMPLÈTE DE STRUCT LENV (avant lval) ===== */
struct lenv {
  lenv* par;       /* environnement parent (pour portées imbriquées) */
  int count;       /* nombre d'entrées */
  char** syms;     /* tableau de chaînes (symboles) */
  lval** vals;     /* tableau de valeurs (lval) */
};
/* ===== FIN STRUCT LENV ===== */

struct lval {
  int type;

  /* Basic */
  float num;         /* pour LVAL_NUM */
  char* err;        /* pour LVAL_ERR */
  char* sym;        /* pour LVAL_SYM */

  /* Fonction */
  lbuiltin builtin; /* pour LVAL_FUN (fonction native) */
  lenv* env;        /* pour LVAL_FUN (environnement de fermeture) */
  lval* formals;    /* pour LVAL_FUN (arguments formels) */
  lval* body;       /* pour LVAL_FUN (corps de la fonction) */

  /* Expression */
  int count;
  lval** cell;
};

/* ===== DÉCLARATIONS ANTICIPÉES DE TOUTES LES FONCTIONS ===== */
lenv* lenv_new(void);
void lenv_del(lenv* e);
lenv* lenv_copy(lenv* e);
void lenv_def(lenv* e, lval* k, lval* v);
lval* lval_fun(lbuiltin func);
lval* builtin_var(lenv* e, lval* a, char* func);
lval* lval_call(lenv* e, lval* f, lval* a);
/* ===== FIN DÉCLARATIONS ANTICIPÉES ===== */

/* Constructeurs simples pour chaque type lval */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}


/* Crée un message d'erreur formaté (va_list) */
lval* lval_err(char* fmt, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  
  /* Crée une liste d'arguments variable et l'initialise */
  va_list va;
  va_start(va, fmt);
  
  /* Alloue 512 octets pour le message d'erreur (taille temporaire) */
  v->err = malloc(512);
  
  /* Formate la chaîne d'erreur avec une limite de 511 caractères */
  vsnprintf(v->err, 511, fmt, va);
  
  /* Réalloue à la taille réellement utilisée pour éviter gaspillage */
  v->err = realloc(v->err, strlen(v->err)+1);
  
  /* Termine l'utilisation de la va_list */
  va_end(va);
  
  return v;
}

/* Crée un symbole (copie la chaîne) */
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/* Crée une fonction lambda (utilisateur) */
lval* lval_lambda(lval* formals, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;

  /* Set Builtin to Null */
  v->builtin = NULL;

  /* Build new environment */
  v->env = lenv_new();

  /* Set Formals and Body */
  v->formals = formals;
  v->body = body;
  return v;
}

/* Crée une fonction native (builtin) */
lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = func;
  v->env = NULL;
  v->formals = NULL;
  v->body = NULL;
  return v;
}

/* Crée une S-expression vide (liste évaluée) */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* Crée une Q-expression vide (liste littérale, manipulable) */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* libération mémoire d'un lval
 *
 * Selon le type, libère les champs alloués et les enfants récursivement.
 */
void lval_del(lval* v) {

  switch (v->type) {
    case LVAL_NUM: break;        /* pas d'allocation dynamique */
    
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_FUN:
    if (!v->builtin) {
      lenv_del(v->env);
      lval_del(v->formals);
      lval_del(v->body);
    }
  break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      /* Supprime récursivement chaque enfant puis le tableau */
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
    break;
  }
  
  free(v);
}

/* Copie profonde d'un lval (utile pour stocker dans lenv) */
lval* lval_copy(lval* v) {

  lval* x = malloc(sizeof(lval));
  x->type = v->type;
  
  switch (v->type) {
    
    /* Copier directement les fonctions et les nombres */
    
    case LVAL_NUM: x->num = v->num; break;
    
    /* Copier les chaînes avec malloc + strcpy */
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->err, v->err); break;
      
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym); break;

    case LVAL_FUN:
      if (v->builtin) {
        x->builtin = v->builtin;
      } else {
        x->builtin = NULL;
        x->env = lenv_copy(v->env);
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
      }
    break;
    
    /* Copier les listes en copiant chaque sous-expression */
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
    break;
  }
  
  return x;
}

lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n->par = e->par;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  for (int i = 0; i < e->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

/* Ajoute un enfant à une liste (sexpr/qexpr) */
lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

/* Concatène y à la fin de x (y est consommé) */
lval* lval_join(lval* x, lval* y) {  
  for (int i = 0; i < y->count; i++) {
    x = lval_add(x, y->cell[i]);
  }
  free(y->cell);
  free(y);  
  return x;
}

/* Retire et retourne l'élément d'index i (ne libère pas l'élément) */
lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];  
  memmove(&v->cell[i], &v->cell[i+1],
    sizeof(lval*) * (v->count-i-1));  
  v->count--;  
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

/* Prend l'élément i et libère la liste d'origine */
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

/* Prototypes d'affichage */
void lval_print(lval* v);

/* Affiche une expression entre open et close, sépare par espaces */
void lval_print_expr(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);    
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}


/* int si nombre entiers, float si nombre décimal (utilise la bibliotheque math.h)*/
void lval_print_num(lval* v) {
  if (v->type == LVAL_NUM) {
    if (fabsf(v->num - roundf(v->num)) < 1e-6f) {
      printf("%d", (int)roundf(v->num));
    } else {
      printf("%g", v->num);
    }
  }
}

/* Affichage en fonction du type */
void lval_print(lval* v) {
  if (v->type == LVAL_SEXPR) {
    // Special case: if it's an arithmetic S-expression in polish notation, print infix
    if (v->count == 3 &&
        v->cell[0]->type == LVAL_SYM &&
        (strcmp(v->cell[0]->sym, "+") == 0 ||
         strcmp(v->cell[0]->sym, "-") == 0 ||
         strcmp(v->cell[0]->sym, "*") == 0 ||
         strcmp(v->cell[0]->sym, "/") == 0)) {

      // Print as infix: operand1 operator operand2
      if (v->cell[1]->type == LVAL_SEXPR || v->cell[1]->type == LVAL_QEXPR) {
        putchar('(');
        lval_print(v->cell[1]);
        putchar(')');
      } else {
        lval_print(v->cell[1]);
      }

      printf(" %s ", v->cell[0]->sym);

      if (v->cell[2]->type == LVAL_SEXPR || v->cell[2]->type == LVAL_QEXPR) {
        putchar('(');
        lval_print(v->cell[2]);
        putchar(')');
      } else {
        lval_print(v->cell[2]);
      }
      return;
    }
  }

  switch (v->type) {
    case LVAL_FUN:
    if (v->builtin) {
      printf("<builtin>");
    } else {
      printf("(\\ "); lval_print(v->formals);
      putchar(' '); lval_print(v->body); putchar(')');
    }
    break;
    case LVAL_NUM:   lval_print_num(v); break; // ça va aller check si c'est int ou float
    case LVAL_ERR:   printf("Erreur: %s", v->err); break;
    case LVAL_SYM:   printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_print_expr(v, '(', ')'); break;
    case LVAL_QEXPR: lval_print_expr(v, '{', '}'); break;
  }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }

// Nom lisible d'un type (utile pour messages d'erreur)
char* ltype_name(int t) {
  switch(t) {
    case LVAL_FUN: return "Fonction";
    case LVAL_NUM: return "Nombre";
    case LVAL_ERR: return "Erreur";
    case LVAL_SYM: return "Symbole";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Inconnu";
  }
}

/* Environnement Lisp : association symbole -> lval
 *
 * Implémentation simple par tableaux dynamiques (grow with realloc).
 */

float lval_eq(lval* x, lval* y) {

  /* Different Types are always unequal */
  if (x->type != y->type) { return 0; }

  /* Compare Based upon type */
  switch (x->type) {
    /* Compare Number Value */
    case LVAL_NUM: return (x->num == y->num);

    /* Compare String Values */
    case LVAL_ERR: return (strcmp(x->err, y->err) == 0);
    case LVAL_SYM: return (strcmp(x->sym, y->sym) == 0);

    /* If builtin compare, otherwise compare formals and body */
    case LVAL_FUN:
      if (x->builtin || y->builtin) {
        return x->builtin == y->builtin;
      } else {
        return lval_eq(x->formals, y->formals)
          && lval_eq(x->body, y->body);
      }

    /* If list compare every individual element */
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      if (x->count != y->count) { return 0; }
      for (int i = 0; i < x->count; i++) {
        /* If any element not equal then whole list not equal */
        if (!lval_eq(x->cell[i], y->cell[i])) { return 0; }
      }
      /* Otherwise lists must be equal */
      return 1;
    break;
  }
  return 0;
}


lenv* lenv_new(void) { // crée un nouvel environnement vide
  lenv* e = malloc(sizeof(lenv)); //  alloue mémoire
  e->par = NULL; // pas de parent par défaut
  e->count = 0; // pas d'entrés
  e->syms = NULL; // pas de symbole
  e->vals = NULL; // pas de valeur
  return e; // return l'environnement créé
}

/* Libère l'environnement et toutes les valeurs stockées */
void lenv_del(lenv* e) {
  
  /* Parcourt tous les éléments de l'environnement et les supprime */
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  
  /* Libère la mémoire allouée pour les listes */
  free(e->syms);
  free(e->vals);
  free(e);
}

/* Recherche la valeur associée à la clé k (symbole) */
lval* lenv_get(lenv* e, lval* k) {
  
  /* Parcourt tous les éléments de l'environnement */
  for (int i = 0; i < e->count; i++) {
    /* Vérifie si la chaîne stockée correspond au symbole demandé */
    /* Si oui, retourne une copie de la valeur (protection contre modifications) */
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }

  /* If no symbol check in parent otherwise error */
  if (e->par) {
    return lenv_get(e->par, k);
  } else {
    return lval_err("Unbound Symbol '%s'", k->sym);
  }

  /* Si aucun symbole trouvé, renvoie une erreur */
  return lval_err("Unbound Symbol '%s'", k->sym);
}

/* Insère ou remplace une valeur pour une clé donnée.
 * Effectue une copie de v pour le stocker dans l'environnement.
 */
void lenv_put(lenv* e, lval* k, lval* v) {
  
  /* Parcourt tous les éléments pour vérifier si la variable existe déjà */
  for (int i = 0; i < e->count; i++) {
  
    /* Si la variable est trouvée, supprime l'ancienne valeur et la remplace */
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }
  
  /* Si aucune entrée existante, alloue de l'espace pour une nouvelle */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);
  
  /* Copie le contenu de lval et la chaîne de symbole dans la nouvelle entrée */
  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym)+1);
  strcpy(e->syms[e->count-1], k->sym);
}

/* Fonctions intégrées (Builtins) */

/* Macros d'assertions pour vérifier les arguments passés aux builtins.
 * Elles libèrent la liste d'arguments et retournent une lval_err en cas d'échec.
 */
#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { lval* err = lval_err(fmt, ##__VA_ARGS__); lval_del(args); return err; }

#define LASSERT_TYPE(func, args, index, expect) \
  LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i. Got %s, Expected %s.", \
    func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
  LASSERT(args, args->count == num, \
    "Function '%s' passed incorrect number of arguments. Got %i, Expected %i.", \
    func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
  LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed {} for argument %i.", func, index);

    

 lval* builtin_lambda(lenv* e, lval* a) {
  /* Check Two arguments, each of which are Q-Expressions */
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

  /* Check first Q-Expression contains only Symbols */
  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
      "Cannot define non-symbol. Got %s, Expected %s.",
      ltype_name(a->cell[0]->cell[i]->type),ltype_name(LVAL_SYM));
  }

  /* Pop first two arguments and pass them to lval_lambda */
  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  lval_del(a);

  return lval_lambda(formals, body);
}


lval* lval_eval_sexpr(lenv* e, lval* v);

lval* lval_eval(lenv* e, lval* v) {
  if (v->type == LVAL_SYM) {
    /* Récupère la valeur du symbole dans l'environnement */
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
  return v;
}

/* builtins */


// Définit des variables dans l'environnement global
lval* builtin_def(lenv* e, lval* a) {
  return builtin_var(e, a, "fonction");
}

// Définit des variables dans l'environnement local
lval* builtin_put(lenv* e, lval* a) {
  return builtin_var(e, a, "=");
}

/* list : transforme des arguments en Q-expression (liste littérale) */
lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

/* head : retourne une Q-expression contenant uniquement le premier élément */
lval* builtin_head(lenv* e, lval* a) {
  LASSERT_NUM("head", a, 1);
  LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("head", a, 0);
  
  /* Prend et retourne le premier élément d'une Q-expression */
  lval* v = lval_take(a, 0);  
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

/* tail : retourne une Q-expression sans son premier élément */
lval* builtin_tail(lenv* e, lval* a) {
  LASSERT_NUM("tail", a, 1);
  LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("tail", a, 0);

  /* Prend et retourne la Q-expression sans son premier élément */
  lval* v = lval_take(a, 0);  
  lval_del(lval_pop(v, 0));
  return v;
}

/* eval : évalue une Q-expression (transforme en S-expression puis évalue) */
lval* builtin_eval(lenv* e, lval* a) {
  LASSERT_NUM("eval", a, 1);
  LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);
  
  /* Convertit une Q-expression en S-expression puis évalue */
  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

/* join : concatène plusieurs Q-expressions */
lval* builtin_join(lenv* e, lval* a) {
  
  /* Vérifie que tous les arguments sont des Q-expressions */
  for (int i = 0; i < a->count; i++) {
    LASSERT_TYPE("join", a, i, LVAL_QEXPR);
  }
  
  /* Concatène toutes les Q-expressions */
  lval* x = lval_pop(a, 0);
  
  while (a->count) {
    lval* y = lval_pop(a, 0);
    x = lval_join(x, y);
  }
  
  lval_del(a);
  return x;
  
}

// Comparaison d'égalité

lval* builtin_ord(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE(op, a, 0, LVAL_NUM);
  LASSERT_TYPE(op, a, 1, LVAL_NUM);

  int r;
  if (strcmp(op, ">")  == 0) {
    r = (a->cell[0]->num >  a->cell[1]->num);
  }
  if (strcmp(op, "<")  == 0) {
    r = (a->cell[0]->num <  a->cell[1]->num);
  }
  if (strcmp(op, ">=") == 0) {
    r = (a->cell[0]->num >= a->cell[1]->num);
  }
  if (strcmp(op, "<=") == 0) {
    r = (a->cell[0]->num <= a->cell[1]->num);
  }
  lval_del(a);
  return lval_num(r);
}

lval* builtin_cmp(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);
  float r;
  if (strcmp(op, "==") == 0) {
    r =  lval_eq(a->cell[0], a->cell[1]);
  }
  if (strcmp(op, "!=") == 0) {
    r = !lval_eq(a->cell[0], a->cell[1]);
  }
  lval_del(a);
  return lval_num(r);
}

lval* builtin_eq(lenv* e, lval* a) {
  return builtin_cmp(e, a, "==");
}

lval* builtin_ne(lenv* e, lval* a) {
  return builtin_cmp(e, a, "!=");
}

// Comparaison arithmétique
lval* builtin_gt(lenv* e, lval* a) {
  return builtin_ord(e, a, ">");
}
lval* builtin_lt(lenv* e, lval* a) {
  return builtin_ord(e, a, "<");
}
lval* builtin_ge(lenv* e, lval* a) {
  return builtin_ord(e, a, ">=");
}
lval* builtin_le(lenv* e, lval* a) {
  return builtin_ord(e, a, "<=");
}
// if : évalue une des deux expressions
lval* builtin_if(lenv* e, lval* a) {
  LASSERT_NUM("if", a, 3);
  LASSERT_TYPE("if", a, 0, LVAL_NUM);
  LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("if", a, 2, LVAL_QEXPR);
 

  /* Utilise LASSERT_TYPE(...,LVAL_FUN) si il y'a une fonction dans la condition*/
  /* exemple : if x > 2 then blablabla  x est considéré comme fonction */ 
  if (a->cell[0]->type == LVAL_FUN) {
    lval* result = lval_call(e, a->cell[0], lval_sexpr());
    lval_del(a->cell[0]);
    a->cell[0] = result;
    if (a->cell[0]->type == LVAL_ERR) {
      lval* err = a->cell[0];
      lval_del(a);
      return err;
    }
    LASSERT_TYPE("if", a, 0, LVAL_NUM);
  }

  /* Mark Both Expressions as evaluable */
  lval* x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  if (a->cell[0]->num) {
    /* If condition is true evaluate first expression */
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    /* Otherwise evaluate second expression */
    x = lval_eval(e, lval_pop(a, 2));
  }

  /* Delete argument list and return */
  lval_del(a);
  return x;
}


/* builtin_op : opérateurs arithmétiques génériques (+ - * /)
 * Opère sur une liste de nombres et gère la division par zéro.
 */
lval* builtin_op(lenv* e, lval* a, char* op) {
  
  /* Vérifie que tous les arguments sont des nombres */
  for (int i = 0; i < a->count; i++) {
    LASSERT_TYPE(op, a, i, LVAL_NUM);
  }
  
  lval* x = lval_pop(a, 0);
  
  /* Si opérateur unaire '-' et aucun autre argument, faire la négation */
  if ((strcmp(op, "-") == 0) && a->count == 0) {
    x->num = -x->num;
  }
  
  /* Applique l'opération sur tous les éléments restants */
  while (a->count > 0) {  
    lval* y = lval_pop(a, 0);
    
    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        /* Gestion d'erreur : division par zéro -> retourne une lval_err */
        lval_del(x); lval_del(y);
        x = lval_err("Division By Zero.");
        break;
      }
      x->num /= y->num;
    }
    
    lval_del(y);
  }
  
  lval_del(a);
  return x;
}

/* Ajoute un builtin dans l'environnement global */
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k); lval_del(v);
}

 lval* lval_call(lenv* e, lval* f, lval* a) {

  /* If Builtin then simply apply that */
  if (f->builtin) { return f->builtin(e, a); }

  /* Record Argument Counts */
  int given = a->count;
  int total = f->formals->count;
  (void)given; (void)total;
  
  /* While arguments still remain to be processed */
  while (a->count) {

    /* Pop the first symbol from the formals */
    lval* sym = lval_pop(f->formals, 0);

    /* Pop the next argument from the list */
    lval* val = lval_pop(a, 0);

    
      /* Special Case to deal with '&' */
    if (strcmp(sym->sym, "&") == 0) {

      /* Ensure '&' is followed by another symbol */
      if (f->formals->count != 1) {
        lval_del(a);
        return lval_err("Function format invalid. "
          "Symbol '&' not followed by single symbol.");
      }

      /* Next formal should be bound to remaining arguments */
    lval* nsym = lval_pop(f->formals, 0);
    lenv_put(f->env, nsym, builtin_list(e, a));
    lval_del(sym); lval_del(nsym);
    break;
  }

    /* If '&' remains in formal list bind to empty list */
    if (f->formals->count > 0 &&
      strcmp(f->formals->cell[0]->sym, "&") == 0) {

      /* Check to ensure that & is not passed invalidly. */
      if (f->formals->count != 2) {
        return lval_err("Function format invalid. "
          "Symbol '&' not followed by single symbol.");
      }

      /* Pop and delete '&' symbol */
      lval_del(lval_pop(f->formals, 0));

      /* Next formal should be bound to remaining arguments */
      lval* nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym); lval_del(nsym);
      break;
    }

    /* Bind a copy into the function's environment */
    lenv_put(f->env, sym, val);

    /* Delete symbol and value */
    lval_del(sym); lval_del(val);
  }

  /* Argument list is now bound so can be cleaned up */
  lval_del(a);

  /* If all formals have been bound evaluate */
  if (f->formals->count == 0) {

    /* Set environment parent to evaluation environment */
    f->env->par = e;

    /* Evaluate and return */
    return builtin_eval(
      f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    /* Otherwise return partially evaluated function */
    return lval_copy(f);
  }

}

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval* syms = a->cell[0];
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
      "Function '%s' cannot define non-symbol. "
      "Got %s, Expected %s.", func,
      ltype_name(syms->cell[i]->type),
      ltype_name(LVAL_SYM));
  }

  LASSERT(a, (syms->count == a->count-1),
    "Function '%s' passed too many arguments for symbols. "
    "Got %i, Expected %i.", func, syms->count, a->count-1);

  for (int i = 0; i < syms->count; i++) {
    /* If 'fonction' define in globally. If 'put' define in locally */
    if (strcmp(func, "fonction") == 0) {
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }

    if (strcmp(func, "=")   == 0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }

  lval_del(a);
  return lval_sexpr();
}


/* wrappers pour opérateurs nommés (utilisés comme symboles) */
lval* builtin_add(lenv* e, lval* a) { return builtin_op(e, a, "+"); }
lval* builtin_sub(lenv* e, lval* a) { return builtin_op(e, a, "-"); }
lval* builtin_mul(lenv* e, lval* a) { return builtin_op(e, a, "*"); }
lval* builtin_div(lenv* e, lval* a) { return builtin_op(e, a, "/"); }

// Définit une variable dans l'environnement global (utilisé par 'def')
void lenv_def(lenv* e, lval* k, lval* v) {
  /* Iterate till e has no parent */
  while (e->par) { e = e->par; }
  /* Put value in e */
  lenv_put(e, k, v);
}

/* Enregistre tous les builtins standard */
void lenv_add_builtins(lenv* e) {
  /* Fonctions de gestion de variables */
  lenv_add_builtin(e, "fonction", builtin_def);
  lenv_add_builtin(e, "=",   builtin_put);

  
  /* Fonctions sur les listes */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);

    /* Comparison Functions */
  lenv_add_builtin(e, "si", builtin_if);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_ne);
  lenv_add_builtin(e, ">",  builtin_gt);
  lenv_add_builtin(e, "<",  builtin_lt);
  lenv_add_builtin(e, ">=", builtin_ge);
  lenv_add_builtin(e, "<=", builtin_le);
  
  /* Fonctions mathématiques */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "\\", builtin_lambda);
}

/* Évaluation */

/* Évalue une S-expression : évaluer les enfants puis appliquer la fonction */
lval* lval_eval_sexpr(lenv* e, lval* v) {
  
  /* Évalue les enfants (post-ordre) */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }
  
  /* Vérification des erreurs parmi les enfants */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }
  
  /* Expression vide -> retourne telle quelle */
  if (v->count == 0) { return v; }  
  /* Expression unique -> retourne l'unique élément */
  if (v->count == 1) { return lval_take(v, 0); }
  
  /* S'assure que le premier élément est une fonction après évaluation.
   * Ici on ne gère que les fonctions natives stockées en tant que LVAL_FUN.
   */
  
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval* err = lval_err(
      "S-Expression starts with incorrect type. "
      "Got %s, Expected %s.",
      ltype_name(f->type), ltype_name(LVAL_FUN));
    lval_del(f); lval_del(v);
    return err;
  }

  /* Appelle la fonction (qui consomme la liste v) et renvoie le résultat */
  lval* result = lval_call(e, f, v);
  lval_del(f);
  return result;
}

lval* lval_read_num(mpc_ast_t* t);

/* Lecture (Parsing)
 *
 * Fonctions qui transforment l'AST mpc en structures lval.
 */

/* Lecture d'un nombre depuis un noeud AST */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("Invalid Number.");
}

/* Lecture générique d'un noeud AST :
 * - number -> lval_num
 * - symbol -> lval_sym
 * - sexpr/qexpr -> construire récursivement
 */
lval* lval_read(mpc_ast_t* t) {
  
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
  
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); } 
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }
  
  /* Parcourt les enfants de l'AST et ignore les délimiteurs */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  
  return x;
}

/* Prétraitement : conversion d'une expression infixée simple en notation préfixée Lisp */
char* preprocess_infix(const char* input) {
  /*
   * Convertit :
   * 1. Les expressions infix arithmétiques : x + y -> (+ x y)
   * 2. Les expressions if-then-else : if cond then expr else expr
   */

  struct {
    const char* word;
    const char* symbol;
  } op_map[] = {
    {"plus", "+"}, {"moin", "-"}, {"multiplier", "*"}, {"multiplier par", "*"}, {"diviser", "/"}, {"diviser par", "/"},
    {"+", "+"}, {"-", "-"}, {"*", "*"}, {"/", "/"},
    {">", ">"}, {"<", "<"}, {">=", ">="}, {"<=", "<="}, {"==", "=="}, {"!=", "!="},
  };

  /* Check for if-then-else pattern at character level */
  if (strncmp(input, "si ", 3) == 0) {
    const char* then_pos = strstr(input, " alors ");
    const char* else_pos = strstr(input, " sinon ");
    
    if (then_pos != NULL) {
      /* Extract condition */
      int cond_len = then_pos - input - 3;
      char* condition = malloc(cond_len + 1);
      if (condition) {
        strncpy(condition, input + 3, cond_len);
        condition[cond_len] = '\0';

        /* Start of then branch */
        const char* then_content = then_pos + 6;
        while (*then_content && isspace((unsigned char)*then_content)) then_content++;
        
        char* then_expr = malloc(4096);
        char* else_expr = malloc(4096);
        
        if (then_expr && else_expr) {
          then_expr[0] = '\0';
          else_expr[0] = '\0';
          
          int then_len = 0;
          
          /* Extract then branch (handle both {expr} and bare expr) */
          if (*then_content == '{') {
            int brace_count = 1;
            const char* ptr = then_content + 1;
            while (*ptr && brace_count > 0) {
              if (*ptr == '{') brace_count++;
              else if (*ptr == '}') brace_count--;
              if (brace_count > 0) {
                then_expr[then_len++] = *ptr;
              }
              ptr++;
            }
            then_expr[then_len] = '\0';
            
            /* Check for else after the closing brace */
            if (else_pos && else_pos >= (ptr - 1)) {
              const char* else_content = else_pos + 6;
              while (*else_content && isspace((unsigned char)*else_content)) else_content++;
              
              int else_len = 0;
              if (*else_content == '{') {
                int brace_count = 1;
                const char* eptr = else_content + 1;
                while (*eptr && brace_count > 0) {
                  if (*eptr == '{') brace_count++;
                  else if (*eptr == '}') brace_count--;
                  if (brace_count > 0) {
                    else_expr[else_len++] = *eptr;
                  }
                  eptr++;
                }
                else_expr[else_len] = '\0';
              } else {
                strcpy(else_expr, else_content);
              }
            }
          } else {
            /* No braces, scan until 'else' or end */
            if (else_pos) {
              then_len = else_pos - then_content;
              strncpy(then_expr, then_content, then_len);
              then_expr[then_len] = '\0';
              /* Trim trailing whitespace */
              while (then_len > 0 && isspace((unsigned char)then_expr[then_len-1])) {
                then_expr[--then_len] = '\0';
              }
              
              const char* else_content = else_pos + 6;
              while (*else_content && isspace((unsigned char)*else_content)) else_content++;
              strcpy(else_expr, else_content);
            } else {
              /* No else, just take rest as then */
              strcpy(then_expr, then_content);
            }
          }

          /* Recursively preprocess parts */
          char* proc_cond = preprocess_infix(condition);
          char* proc_then = preprocess_infix(then_expr);
          char* proc_else = preprocess_infix(else_expr);

          char* result = malloc(8192);
          if (result) {
            snprintf(result, 8192, "(si %s {%s} {%s})",
                     proc_cond, proc_then, proc_else);
            
            free(condition);
            free(then_expr);
            free(else_expr);
            free(proc_cond);
            free(proc_then);
            free(proc_else);
            return result;
          }
          
          free(then_expr);
          free(else_expr);
        }
        free(condition);
      }
    }
  }

  /* Tokenize for arithmetic infix */
  char* input_copy = malloc(strlen(input) + 1);
  if (!input_copy) return NULL;
  strcpy(input_copy, input);
  
  char** tokens = NULL;
  int tokens_count = 0;
  char* saveptr = NULL;
  char* token = strtok_r(input_copy, " ", &saveptr);
  
  while (token != NULL) {
    char** tmp = realloc(tokens, sizeof(char*) * (tokens_count + 1));
    if (!tmp) { free(tokens); free(input_copy); return NULL; }
    tokens = tmp;
    tokens[tokens_count++] = token;
    token = strtok_r(NULL, " ", &saveptr);
  }

  if (tokens_count == 0) {
    free(tokens);
    free(input_copy);
    return strdup("");
  }

  if (tokens_count < 3) {
    char* out = strdup(input);
    free(tokens);
    free(input_copy);
    return out;
  }

  int is_op = 0;
  for (size_t m = 0; m < sizeof(op_map)/sizeof(op_map[0]); m++) {
    if (strcmp(op_map[m].word, tokens[1]) == 0) { is_op = 1; break; }
  }

  if (!is_op) {
    char* out = strdup(input);
    free(tokens);
    free(input_copy);
    return out;
  }

  char* left = malloc(strlen(tokens[0]) + 1);
  strcpy(left, tokens[0]);

  for (int i = 1; i + 1 < tokens_count; i += 2) {
    const char* op = tokens[i];
    const char* op_sym = op;
    for (size_t m = 0; m < sizeof(op_map)/sizeof(op_map[0]); m++) {
      if (strcmp(op_map[m].word, op) == 0) { op_sym = op_map[m].symbol; break; }
    }

    char* right = malloc(strlen(tokens[i+1]) + 1);
    strcpy(right, tokens[i+1]);

    int size = (int)(strlen(op_sym) + strlen(left) + strlen(right) + 6);
    char* combined = malloc(size);
    if (!combined) { free(left); free(right); free(tokens); free(input_copy); return NULL; }

    snprintf(combined, size, "(%s %s %s)", op_sym, left, right);

    free(left);
    free(right);
    left = combined;
  }

  free(tokens);
  free(input_copy);
  return left;
}

/* Fonction principale : initialise le parser, l'environnement, lance la boucle REPL */
static mpc_parser_t* Number;
static mpc_parser_t* Symbol;
static mpc_parser_t* Sexpr;
static mpc_parser_t* Qexpr;
static mpc_parser_t* Expr;
static mpc_parser_t* Lispy;

int main(int argc, char** argv) {

  puts("Chocolatine Version 0.5");
  puts("Press Ctrl+c to Exit\n");
  
  /* Création des parsers mpc pour le langage */
  Number = mpc_new("number");
  Symbol = mpc_new("symbol");
  Sexpr  = mpc_new("sexpr");
  Qexpr  = mpc_new("qexpr");
  Expr   = mpc_new("expr");
  Lispy  = mpc_new("lispy");
  
  /* Définition de la grammaire en une seule chaîne (operator supprimé) */
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number   : /-?[0-9]+/ ;                             \
      symbol   : /[a-zA-Z0-9_+\\-*\\/=<>!&\\\\]+/ ;        \
      sexpr    : '(' <expr>* ')' ;                        \
      qexpr    : '{' <expr>* '}' ;                        \
      expr     : <number> | <symbol> | <sexpr> | <qexpr> ;\
      lispy    : /^/ <expr>* /$/ ;                        \
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  /* Environnement global et enregistrement des fonctions natives */
  lenv* e = lenv_new();
  lenv_add_builtins(e);
  
  /* Boucle REPL : lire, prétraiter infix, évaluer, afficher */
  while (1) {
  
    char* input = readline("Chocolatine> ");
    add_history(input);
    
    // Prétraiter l'entrée pour convertir infix vers prefix
    char* preprocessed = preprocess_infix(input);
    free(input);
    
    mpc_result_t r;
    if (mpc_parse("<stdin>", preprocessed, Lispy, &r)) {
      /* Lire l'AST, évaluer avec l'environnement, afficher et libérer */
      lval* x = lval_eval(e, lval_read(r.output));
      
      lval_println(x);
      
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {    
      /* Afficher l'erreur de parsing (mpc) et nettoyer */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    free(preprocessed);
    
  }
  
  /* Nettoyage final (jamais atteint dans le REPL infini sans signal) */
  lenv_del(e);
  
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  
  return 0;
}
