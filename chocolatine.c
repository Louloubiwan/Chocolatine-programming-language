/*
 * Chocolatine - projet commencé en Aout 2025
   Merci à OrangeDuck pour les libarary mpc
   
 */

#include "mpc.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>





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

void add_history(char* unused) {
    // du vide
}

#else
#include <readline/readline.h>
#include <readline/history.h>


#endif

// Déclarations anticipées */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

// Fonction principale : initialise le parser, l'environnement
// Parsers globaux pour l'analyse
static mpc_parser_t* Number;
static mpc_parser_t* Bool;
static mpc_parser_t* Symbol;
static mpc_parser_t* String;
static mpc_parser_t* Comment;
static mpc_parser_t* Sexpr;
static mpc_parser_t* Qexpr;
static mpc_parser_t* Expr;
static mpc_parser_t* Lispy;
static mpc_parser_t* Call;


// Type de fonction builtin : prend l'environnement et une liste d'arguments
typedef lval*(*lbuiltin)(lenv*, lval*);

// Énumération des types de valeurs Lisp
enum {  
  LVAL_ERR,    // Erreur 
  LVAL_NUM,    // Nombre 
  LVAL_SYM,    // Symbole 
  LVAL_FUN,    // Fonction 
  LVAL_STR,    // Chaîne de caractères 
  LVAL_BOOL,   // Booléen 
  LVAL_SEXPR,  // S-expression (evaluated list)
  LVAL_QEXPR   // Q-expression (quoted list) 
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
  double num;         /* pour LVAL_NUM */
  char* err;        /* pour LVAL_ERR */
  char* sym;        /* pour LVAL_SYM */
  char* str;        /* pour LVAL_STR */
  bool* bools;       /* pour LVAL_BOOL */

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
lval* lval_read(mpc_ast_t* t);
char* preprocess_infix(const char* input);

/* ===== FIN DÉCLARATIONS ANTICIPÉES ===== */

/* Constructeurs simples pour chaque type lval */
lval* lval_num(double x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_str(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(s) + 1);
  strcpy(v->str, s);
  return v;
}

lval* lval_bool(bool b) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_BOOL;
  v->bools = malloc(sizeof(bool));
  *(v->bools) = b;
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
    case LVAL_STR: free(v->str); break;
    case LVAL_BOOL: free(v->bools); break;

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

    case LVAL_STR: 
      x->str = malloc(strlen(v->str) + 1);
      strcpy(x->str, v->str); break;

    case LVAL_BOOL:
      x->bools = malloc(sizeof(bool));
      *(x->bools) = *(v->bools);
      break;
     
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

void lval_print_str(lval* v) {
  /* Make a Copy of the string */
  char* escaped = malloc(strlen(v->str)+1);
  strcpy(escaped, v->str);
  /* Pass it through the escape function */
  escaped = mpcf_escape(escaped);
  /* Print it between " characters */
  printf("%s", escaped);
  /* free the copied string */
  free(escaped);
}


/* int si nombre entiers, float si nombre décimal (utilise la bibliotheque math.h)*/
void lval_print_num(lval* v) {
  if (v->type == LVAL_NUM) {
    // Si c'est un entier (ex: 5.0), affiche sans décimale
    if (fmod(v->num, 1.0) == 0.0) {
      printf("%ld", (long)v->num);
    } else {
      printf("%g", v->num); // Affiche avec décimales si nécessaire
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
    case LVAL_STR:  lval_print_str(v); break;
    case LVAL_BOOL: printf("%s", *(v->bools) ? "vrai" : "faux"); break;

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
    case LVAL_STR: return "Caractere";
    case LVAL_BOOL: return "Booléen";
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

    case LVAL_STR: return (strcmp(x->str, y->str) == 0);

    case LVAL_BOOL: return (*(x->bools) == *(y->bools));

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

// ---------------------------------------------------------------------------//
// FIN DES DÉFINITIONS DE LENV ET LVAL (FONCTIONS COEUR) //




















// ---------------------------------------------------------------------------//
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
  LASSERT_TYPE("fonction", a, 0, LVAL_QEXPR);

  lval* syms = a->cell[0];

  // Cas 1 : Définition de variable (ancien comportement)
  if (syms->count == 1 && a->count == 2) {
    // Ex: fonction {x} 10 → variable
    lenv_def(e, syms->cell[0], a->cell[1]);
    lval_del(a);
    return lval_str(" ");
  }

  // Cas 2 : Définition de fonction (nouveau comportement)
  if (syms->count == 1 && a->count == 3) {
    LASSERT_TYPE("fonction", a, 1, LVAL_QEXPR);  // paramètres
    LASSERT_TYPE("fonction", a, 2, LVAL_QEXPR);  // corps

    // Vérifier que le nom est un symbole
    LASSERT(a, syms->cell[0]->type == LVAL_SYM,
      "Function name must be a symbol. Got %s", ltype_name(syms->cell[0]->type));

    // Créer la fonction
    lval* formals = lval_copy(a->cell[1]);
    lval* body = lval_copy(a->cell[2]);
    lval* func = lval_lambda(formals, body);

    // Stocker dans l'environnement global
    lenv_def(e, syms->cell[0], func);

    // Nettoyer
    lval_del(a);
    lval_del(func);
    return lval_str(" ");
  }

  // Cas 3 : Erreur — nombre d'arguments incorrect
  LASSERT(a, 0, "fonction expects 1 or 2 arguments for variable, or 3 for function. Got %i.", a->count);
  return lval_err("Invalid syntax for fonction");
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

lval* builtin_error(lenv* e, lval* a) {
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);

  // trouve l'erreur et apparatient au string
  lval* err = lval_err(a->cell[0]->str);

  // return l'error
  lval_del(a);
  return err;
}

lval* builtin_print(lenv* e, lval* a) {
  
  for (int i = 0; i < a->count; i++) {
    lval_print(a->cell[i]);
  }

  // nouvelle ligne
  putchar('\n');
  lval_del(a);

  return lval_str(" ");
}

lval* builtin_load(lenv* e, lval* a) {
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  /* Read file contents */
  FILE* f = fopen(a->cell[0]->str, "r");
  if (f == NULL) {
    lval* err = lval_err("Erreur pendant l'ouverture du fichier %s", a->cell[0]->str);
    lval_del(a);
    return err;
  }
  
  // Évaluer fichier ligne par ligne
  char linebuf[8192];
  lval* resultat_evaluee = NULL;


  while (fgets(linebuf, sizeof(linebuf), f) != NULL) {
    // Supprime les caractères de nouvelle ligne, on prends les ligne vide et remplace avec \0 pour NULL
    size_t len = strcspn(linebuf, "\r\n");
    linebuf[len] = '\0'; 
   

    // Skip les lignes vides
    char* p = linebuf; // pointe sur le début de la ligne
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') { continue; }


    char* preprocessed = preprocess_infix(linebuf);
    
    mpc_result_t r;
    if (mpc_parse("<file>", preprocessed, Lispy, &r)) {
      lval* expr = lval_eval(e, lval_read(r.output));
      
      lval_println(expr);
      lval_del(expr);
      mpc_ast_delete(r.output);
      

      free(preprocessed);
    } else {
      // clean
      char* err_msg = mpc_err_string(r.error);
      mpc_err_delete(r.error);
      lval* err = lval_err("n'a pas pu charger le fichier %s", err_msg);
      free(err_msg);
      free(preprocessed);
      lval_del(a);
      fclose(f);
      return err;
    }
  }

  fclose(f);
  lval_del(a);

  // si rien est evalué alors retourné un string vide
  if (!resultat_evaluee) return lval_str(" ");
  return resultat_evaluee;
}
// Comparaison d'égalités

lval* builtin_ord(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE(op, a, 0, LVAL_NUM);
  LASSERT_TYPE(op, a, 1, LVAL_NUM);

  int r;
  if (op[0] == '>') r = (op[1] == '=') ? (a->cell[0]->num >= a->cell[1]->num) : (a->cell[0]->num > a->cell[1]->num);
  else if (op[0] == '<') r = (op[1] == '=') ? (a->cell[0]->num <= a->cell[1]->num) : (a->cell[0]->num < a->cell[1]->num);
  else r = 0;
  
  lval_del(a);
  return lval_bool(r);
}

lval* builtin_cmp(lenv* e, lval* a, char* op) {
  LASSERT_NUM(op, a, 2);
  float r;
  if (op[0] == '=') r = lval_eq(a->cell[0], a->cell[1]);
  else if (op[0] == '!') r = !lval_eq(a->cell[0], a->cell[1]);
  else r = 0;
  
  lval_del(a);
  return lval_bool(r);
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

// check si la valeur n'est pas bool
static int is_truthy(lval* v) {
  if (v->type == LVAL_NUM) return v->num != 0;
  if (v->type == LVAL_BOOL) return *(v->bools);
  if (v->type == LVAL_SYM) return v->sym[0] != '\0'; // non-empty symbol is truthy
  if (v->type == LVAL_STR) return v->str[0] != '\0'; // non-empty string is truthy
  return 0;
}

// Logical AND: returns true si tous les arguments sont vrais
lval* builtin_and(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM && a->cell[i]->type != LVAL_BOOL) {
      LASSERT_TYPE("et", a, i, LVAL_NUM);
    }
    if (!is_truthy(a->cell[i])) { // si un argument est faux
      lval_del(a);
      return lval_bool(0);
    }
  }
  lval_del(a);
  return lval_bool(1);
}

// Logical ou: returns si un argument est vrais
lval* builtin_or(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM && a->cell[i]->type != LVAL_BOOL) {
      LASSERT_TYPE("ou", a, i, LVAL_NUM);
    }
    if (is_truthy(a->cell[i])) { // si un arguments est vrai
      lval_del(a);
      return lval_bool(1);
    }
  }
  lval_del(a);
  return lval_bool(0);
}

// boucle while 

lval* builtin_while(lenv* e, lval* a) {

  // syntax  : tant que {condition} alors {consequence}
  LASSERT_NUM("tant_que", a, 2);
  LASSERT_TYPE("tant_que", a, 0, LVAL_QEXPR); // condition
  LASSERT_TYPE("tant_que", a, 1, LVAL_QEXPR); // consequence

  lval* result = lval_str(" ");

  /* Évalue la condition : copie la qexpr, la convertit en sexpr, évalue */
  lval* cond_copy = lval_copy(a->cell[0]);
  cond_copy->type = LVAL_SEXPR;
  lval* cond = lval_eval(e, cond_copy);

  if (cond->type == LVAL_ERR) {
  lval_del(result);
  lval_del(a);
  return cond;
  }

  while (1) {

    /* Vérifie que le résultat est NUM ou BOOL */
    if (cond->type != LVAL_NUM && cond->type != LVAL_BOOL) {
      lval_del(cond);
      lval_del(result);
      lval_del(a);
      return lval_err("tant_que: condition doit être un nombre ou booléen, got %s",
                      ltype_name(cond->type));
    }

    if (!is_truthy(cond)) {
      lval_del(cond);
      break;
    }
    
    /* Évalue le corps */
    lval* body_copy = lval_copy(a->cell[1]);
    body_copy->type = LVAL_SEXPR;
    lval_del(result);
    result = lval_eval(e, body_copy);

    if (result->type == LVAL_ERR) { break; }
  }

  lval_del(a);
  return result;
}


// boucle for

  // Syntaxe : (pour {i} {0 10} {corps})
lval* builtin_for(lenv* e, lval* a) {
  LASSERT_NUM("pour", a, 3);
  LASSERT_TYPE("pour", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("pour", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("pour", a, 2, LVAL_QEXPR);

  /* Variable */
  LASSERT(a, a->cell[0]->count == 1 && a->cell[0]->cell[0]->type == LVAL_SYM,
    "pour: la variable doit être un symbole unique");
  char* var_name = a->cell[0]->cell[0]->sym;

  /* Évalue début et fin (supporte variables et nombres) */
  LASSERT(a, a->cell[1]->count == 2,
    "pour: la plage doit avoir 2 éléments");

  lval* start_val = lval_eval(e, lval_copy(a->cell[1]->cell[0]));
  lval* end_val   = lval_eval(e, lval_copy(a->cell[1]->cell[1]));

  LASSERT(a, start_val->type == LVAL_NUM,
    "pour: début doit être un nombre, got %s", ltype_name(start_val->type));
  LASSERT(a, end_val->type == LVAL_NUM,
    "pour: fin doit être un nombre, got %s", ltype_name(end_val->type));

  int start = (int)start_val->num;
  int end   = (int)end_val->num;
  lval_del(start_val);
  lval_del(end_val);

  lval* body = a->cell[2];
  lval* result = lval_str(" ");

  for (int i = start; i < end; i++) {
    /* Définit i dans l'environnement */
    lval* var_sym = lval_sym(var_name);
    lval* var_val = lval_num(i);
    lenv_put(e, var_sym, var_val);
    lval_del(var_sym);
    lval_del(var_val);

    /* Évalue le corps */
    lval* body_copy = lval_copy(body);
    body_copy->type = LVAL_SEXPR;
    lval_del(result);
    result = lval_eval(e, body_copy);

    if (result->type == LVAL_ERR) { break; }
  }

  lval_del(a);
  return result;
}


// if : évalue une des deux expressions en fonction d'une condition
lval* builtin_if(lenv* e, lval* a) {
  LASSERT_NUM("si", a, 3);
  LASSERT_TYPE("si", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("si", a, 2, LVAL_QEXPR);
 
  lval* cond = a->cell[0];
  int is_true = 0;
  
  /* Évalue la condition si c'est une fonction */
  if (cond->type == LVAL_FUN) {
    lval* result = lval_call(e, cond, lval_sexpr());
    lval_del(a->cell[0]);
    a->cell[0] = result;
    if (a->cell[0]->type == LVAL_ERR) {
      lval* err = a->cell[0];
      lval_del(a);
      return err;
    }
    cond = a->cell[0];
  }
  
  /* Accepte LVAL_NUM ou LVAL_BOOL */
  if (cond->type == LVAL_NUM || cond->type == LVAL_BOOL) {
    is_true = is_truthy(cond);
  } else {
    lval* err = lval_err("Condition must be number or boolean, got %s", ltype_name(cond->type));
    lval_del(a);
    return err;
  }

  /* Convertit les branches en S-expressions et évalue */
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  lval* result = lval_eval(e, lval_pop(a, is_true ? 1 : 2));
  lval_del(a);
  return result;  
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
  if ((op[0] == '-') && a->count == 0) {
    x->num = -x->num;
  }
  
  /* Applique l'opération sur tous les éléments restants */
  while (a->count > 0) {  
    lval* y = lval_pop(a, 0);
    
    if (op[0] == '+') { x->num += y->num; }
    if (op[0] == '-') { x->num -= y->num; }
    if (op[0] == '*') { x->num *= y->num; }
    if (op[0] == '/') {
      if (fabs(y->num) < 1e-10)  { // marge pour les float
        /* Gestion d'erreur : division par zéro -> retourne une lval_err */
        lval_del(x); lval_del(y);
        return lval_err("Division By Zero.");
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

  /* While arguments still remain to be processed */
  while (a->count) {

    /* If we've run out of formal arguments to bind */
    if (f->formals->count == 0) {
      lval_del(a);
      return lval_err("Function passed too many arguments. Got %i, Expected %i.", given, total);
    }

    /* Pop the first symbol from the formals */
    lval* sym = lval_pop(f->formals, 0);

    /* Pop the next argument from the list */
    lval* val = lval_pop(a, 0);

    /* Bind a copy into the function's environment */
    lenv_put(f->env, sym, val);
    
    lval_del(sym);
    lval_del(val);
  }

  /* Argument list is now bound so can be cleaned up */
  lval_del(a);

  /* If all formals have been bound evaluate */
  if (f->formals->count == 0) {

    /* Set environment parent to evaluation environment */
    f->env->par = e;

    /* Evaluate and return */
    return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  }
  else {
    /* Otherwise return partially evaluated function */
    return lval_copy(f);
  }
}   

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval* syms = a->cell[0];

  LASSERT(a, (syms->count == a->count-1),
    "La fonction '%s' passed too many arguments for symbols. "
    "Got %i, Expected %i.", func, syms->count, a->count-1);

  for (int i = 0; i < syms->count; i++) {
    /* If 'fonction' define in globally. If 'put' define in locally */
    if (strcmp(func, "fonction") == 0) {
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }

    if (strcmp(func, "=")  == 0) {
      lenv_put(e, syms->cell[i], a->cell[i+1]);
      printf("La variable '%s' est definie localement.\n", syms->cell[i]->sym);
    }
  }

  lval_del(a);
  return lval_str(" ");
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

  lenv_add_builtin(e, "load",  builtin_load);
  lenv_add_builtin(e, "lire",  builtin_load);
  lenv_add_builtin(e, "charger",  builtin_load);
  lenv_add_builtin(e, "error", builtin_error);
        
  lenv_add_builtin(e, "afficher", builtin_print);
  lenv_add_builtin(e, "Afficher", builtin_print);

  lenv_add_builtin(e, "pour", builtin_for);
  lenv_add_builtin(e, "tant_que", builtin_while);


  
  /* Fonctions sur les listes */
  lenv_add_builtin(e, "list", builtin_list);
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
  
  /* Logical Functions */
  lenv_add_builtin(e, "et", builtin_and);
  lenv_add_builtin(e, "ou", builtin_or);
  
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
  if (v->count == 0) { return lval_str(" "); }  
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

/* Lecture d'un nombre depuis un noeud mpc_AST */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  char* endptr;
  double x = strtod(t->contents, &endptr);
  if (errno != ERANGE && *endptr == '\0') {
    return lval_num(x);
  }
  return lval_err("Invalid Number: %s", t->contents);
}

lval* lval_read_str(mpc_ast_t* t) {
  /* Cut off the final quote character */
  t->contents[strlen(t->contents)-1] = '\0';
  /* Copy the string missing out the first quote character */
  char* unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  /* Pass through the unescape function */
  unescaped = mpcf_unescape(unescaped);
  /* Construct a new lval using the string */
  lval* str = lval_str(unescaped);
  /* Free the string and return */
  free(unescaped);
  return str;
}

/* Convert call parameters to qexpr - extracts symbols from (x y z) */
lval* lval_call_to_qexpr(lval* call_sexpr) {
  lval* params = lval_qexpr();
  
  /* The call sexpr should have symbols as children */
  for (int i = 0; i < call_sexpr->count; i++) {
    if (call_sexpr->cell[i]->type == LVAL_SYM) {
      params = lval_add(params, lval_copy(call_sexpr->cell[i]));
    }
  }
  
  return params;
}

/* Lecture générique d'un noeud AST */
lval* lval_read(mpc_ast_t* t) {
  
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "bool")) { 
    if (strcmp(t->contents, "vrai") == 0) {
      return lval_bool(1);
    } else if (strcmp(t->contents, "faux") == 0) {
      return lval_bool(0);
    } else {
      return lval_err("Invalid boolean value: %s", t->contents);
    }
  }
  
  /* Handle call nodes - convert to sexpr */
  if (strstr(t->tag, "call")) {
    lval* x = lval_sexpr();
    for (int i = 0; i < t->children_num; i++) {
      if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
      if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
      if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
      x = lval_add(x, lval_read(t->children[i]));
    }
    return x;
  }

  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
  if (strstr(t->tag, "string")) { return lval_read_str(t); }

  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strcmp(t->tag, "<") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }
  
  /* Build expression by adding children */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    if (strstr(t->children[i]->tag, "comment")) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  
  /* POST-PROCESSING: Detect function definition pattern and fix it */
  if (x && x->type == LVAL_SEXPR && x->count >= 3) {
    /* Pattern: (fonction {name} (params...) body...) */
    if (x->cell[0]->type == LVAL_SYM && strcmp(x->cell[0]->sym, "fonction") == 0) {
      if (x->cell[1]->type == LVAL_QEXPR && x->cell[1]->count == 1) {
        /* Check if cell[2] is a sexpr from a call (contains symbols) */
        if (x->cell[2]->type == LVAL_SEXPR) {
          /* Check if it looks like parameters (all symbols) */
          int all_symbols = 1;
          for (int i = 0; i < x->cell[2]->count; i++) {
            if (x->cell[2]->cell[i]->type != LVAL_SYM) {
              all_symbols = 0;
              break;
            }
          }
          
          /* If all symbols, convert to qexpr and wrap remaining elements in body */
          if (all_symbols && x->cell[2]->count > 0) {
            lval* params_qexpr = lval_call_to_qexpr(x->cell[2]);
            lval_del(x->cell[2]);
            
            /* Now wrap all remaining elements (from index 3 onward) into a single qexpr for body */
            lval* body_qexpr = lval_qexpr();
            for (int i = 3; i < x->count; i++) {
              body_qexpr = lval_add(body_qexpr, x->cell[i]);
            }
            
            /* Rebuild x with: fonction, {name}, {params}, {body} */
            lval* new_x = lval_sexpr();
            new_x = lval_add(new_x, x->cell[0]);  // fonction
            new_x = lval_add(new_x, x->cell[1]);  // {name}
            new_x = lval_add(new_x, params_qexpr); // {params}
            new_x = lval_add(new_x, body_qexpr);   // {body}
            
            /* Free old cells array but not the cells we're reusing */
            free(x->cell);
            free(x);
            
            return new_x;
          }
        }
      }
    }
  }
  
  return x;
}

// ---------------------------------------------------------------------------//
// FIN DES FONCTIONS BUILTIN //

















// ---------------------------------------------------------------------------//
/* LES FONCTIONS NECESSAIRES POUR PREPROCESSE_INFIX*/

typedef struct {
  const char* word;
  const char* symbol;
} entree_op;


static void combine_tokens(char** tok, int* cnt, int idx, const char* op_sym) {
  int size = strlen(op_sym) + strlen(tok[idx]) + strlen(tok[idx+2]) + 6;
  char* combined = malloc(size);
  snprintf(combined, size, "(%s %s %s)", op_sym, tok[idx], tok[idx+2]);
  free(tok[idx]); free(tok[idx+1]); free(tok[idx+2]);
  tok[idx] = combined;
  for (int j = idx + 1; j + 2 < *cnt; j++) tok[j] = tok[j + 2];
  *cnt -= 2;
}

static const char* get_op_sym(const char* name, entree_op *op_map, size_t size) {
  for (size_t m = 0; m < size; m++) {
    if (strcmp(op_map[m].word, name) == 0) return op_map[m].symbol; // Si trouvé, retourne le symbole (!=, ==, +, blabla)
  }
  return name;
}

static int is_comparison_op(const char* name) { 
  return strcmp(name, "et") != 0 && strcmp(name, "ou") != 0;
}


//prétraitement : conversion d'une expression infixée simple en notation préfixée Lisp
char* preprocess_infix(const char* input) {



// Normalise tant que à tant_que
  {
    char* tmp = malloc(strlen(input) + 1);
    strcpy(tmp, input);
    char* pos = tmp;
    while ((pos = strstr(pos, "tant que")) != NULL) {
      memcpy(pos, "tant_que ", 8);
      pos += 8;
    }
    
    if (strcmp(tmp, input) != 0) {
      char* r = preprocess_infix(tmp);
      free(tmp);
      return r;
    }
    free(tmp);
  }


  if (strncmp(input, "pour ", 5) == 0) {
    const char* entre_pos = strstr(input, " entre ");
    const char* faire_pos = strstr(input, " faire ");

    if (entre_pos != NULL && faire_pos != NULL) {
      int var_len = entre_pos - (input + 5);
      char* var_name = malloc(var_len + 1);
      strncpy(var_name, input + 5, var_len);
      var_name[var_len] = '\0';

      const char* range_start = entre_pos + 7;
      int range_len = faire_pos - range_start;
      char* range = malloc(range_len + 1);
      strncpy(range, range_start, range_len);
      range[range_len] = '\0';

      const char* body_content = faire_pos + 7;
      while (*body_content && isspace((unsigned char)*body_content)) body_content++;

      char* proc_body;
      if (*body_content == '{') {
        int blen = strlen(body_content);
        char* inner = malloc(blen);
        strncpy(inner, body_content + 1, blen - 2);
        inner[blen - 2] = '\0';
        proc_body = preprocess_infix(inner);
        free(inner);
      } else {
        proc_body = preprocess_infix(body_content);
      }

      char* result = malloc(strlen(var_name) + strlen(range) + strlen(proc_body) + 32);
      snprintf(result, strlen(var_name) + strlen(range) + strlen(proc_body) + 32,
               "(pour {%s} {%s} {%s})", var_name, range, proc_body);

      free(var_name);
      free(range);
      free(proc_body);
      return result;
    }
  }



  if (strncmp(input, "fonction ", 9) == 0) {
    // Pattern: fonction {name} (params) body
    // or: fonction {name}(params) body
    const char* after_fonction = input + 9;
    while (*after_fonction && isspace(*after_fonction)) after_fonction++;
    
    if (*after_fonction == '{') {
      const char* name_end = strchr(after_fonction, '}');
      if (name_end) {
        // Extract name with braces
        int name_len = name_end - after_fonction + 1;
        char* name = malloc(name_len + 1);
        strncpy(name, after_fonction, name_len);
        name[name_len] = '\0';
        
        // Look for opening parent
        const char* params_start = strchr(name_end, '(');
        if (params_start) {
          const char* params_end = strchr(params_start, ')');
          if (params_end) {
            // Extract params
            int params_len = params_end - params_start + 1;
            char* params = malloc(params_len + 1);
            strncpy(params, params_start, params_len);
            params[params_len] = '\0';
            
            // Get body
            const char* body_start = params_end + 1;
            while (*body_start && isspace(*body_start)) body_start++;
            
            // Preprocess the body
            char* proc_body = preprocess_infix(body_start);
            
            // Build result: fonction {name} {params as symbols} {preprocessed body}
            // Convert (x y) to {x y}
            char* params_as_qexpr = malloc(params_len + 2);
            params_as_qexpr[0] = '{';
            strncpy(params_as_qexpr + 1, params_start + 1, params_len - 2);
            params_as_qexpr[params_len - 1] = '}';
            params_as_qexpr[params_len] = '\0';
            
            char* result = malloc(strlen(name) + strlen(params_as_qexpr) + strlen(proc_body) + 32);
            snprintf(result, strlen(name) + strlen(params_as_qexpr) + strlen(proc_body) + 32,
                     "fonction %s %s {%s}", name, params_as_qexpr, proc_body);
            
            free(name);
            free(params);
            free(params_as_qexpr);
            free(proc_body);
            return result;
          }
        }
      }
    }
  }
  
  // ajoute les espace entre les opérateur (on passe de =23 à 2 = 3)
  {
    char* input_copy = malloc(strlen(input) + 1);
    if (input_copy) {
      strcpy(input_copy, input);
      char* saveptr = NULL;
      char* first = strtok_r(input_copy, " ", &saveptr);
      char* second = strtok_r(NULL, " ", &saveptr);
      
      if (first && second && (strcmp(second, "=") == 0)) {
        const char* rest = input + strlen(first) + 1;
        while (*rest && isspace((unsigned char)*rest)) rest++;
        rest = rest + strlen(second);
        while (*rest && isspace((unsigned char)*rest)) rest++;
        
        char* result = malloc(strlen(first) + strlen(rest) + 32);   
        if (result) {
          snprintf(result, strlen(first) + strlen(rest) + 32, "(= {%s} %s)", first, rest);
          free(input_copy);
          return result;
        }
      }
      free(input_copy);
    }
  }

  /* Support des booléens */
  if (strcmp(input, "vrai") == 0 || strcmp(input, "Vrai") == 0 || strcmp(input, "vrais") == 0 || strcmp(input, "Vrais") == 0 || strcmp(input, "vraie") == 0 || strcmp(input, "Vraie") == 0 
      || strcmp(input, "faux") == 0 || strcmp(input, "Faux") == 0){
    return strdup(input);
  }

  entree_op list_caractere[] = {
    {"+", "+"}, {"plus", "+"}, {"Plus", "+"},
    {"-", "-"}, {"moin", "-"}, {"moins", "-"}, {"Moins", "-"}, {"soustraire", "-"}, {"soustrair", "-"}, {"Soustraire", "-"},
    {"*", "*"}, {"multiplier", "*"}, {"multiplier par", "*"}, {"fois", "*"}, {"fois par", "*"}, {"multiplie par", "*"}, {"multiplie", "*"},
    {"/", "/"}, {"diviser", "/"}, {"diviser par", "/"}, {"divise", "/"}, {"divise par", "/"}, {"divisee", "/"}, {"divisee par", "/"},  
       
    {">", ">"}, {"est superieur a", ">"}, {"est superieur que", ">"},
    {"<", "<"}, {"est inferieur a", "<"}, {"est inferieur que", "<"},
    {">=", ">="}, {"est superieur ou egal a", ">="}, {"est superieur ou egale a", ">="}, {"sont superieur ou egale a", ">="}, {"sont superieur ou egaux a", ">="},
    {"<=", "<="}, {"est inferieur ou egal a", "<="}, {"est inferieur ou egale a", "<="}, {"sont inferieur ou egale a", "<="}, {"sont inferieur ou egaux a", "<="},
    {"==", "=="}, {"est", "=="}, {"egale", "=="}, {"egal", "=="}, {"est egale a", "=="}, {"est egal", "=="}, {"sont egale a", "=="}, {"sont egaux a", "=="}, {"n est pas different de", "=="}, 
    
    {"!=", "!="}, {"=!","!="}, {"est different de", "!="}, {"sont differents de", "!="}, {"sont different de", "!="}, {"n est pas", "!="}, {"n est pas egale a", "!="}, {"ne sont pas egaux a", "!="}, 
    {"ne sont pas egale a", "!="}, {"ne sont pas egal a", "!="}, {"ne sont pas egal", "!="}, {"n est pas egales a", "!="}, {"n est pas egal", "!="}, {"n est pas egal a", "!="},

    {"et", "et"}, {"avec", "et"}, {"et aussi", "et"},
    {"ou", "ou"}, 
  };

    // Ex: add(10 20) → (add 10 20)
  char* call_start = strchr(input, '(');
  if (call_start && call_start > input) {
    char* func_name = malloc(call_start - input + 1);
    strncpy(func_name, input, call_start - input);
    func_name[call_start - input] = '\0';

    const char* args_start = call_start + 1;
    const char* args_end = strchr(args_start, ')');
    if (!args_end) {
      free(func_name);
      return strdup(input);
    }

    int args_len = args_end - args_start;
    char* args = malloc(args_len + 1);
    strncpy(args, args_start, args_len);
    args[args_len] = '\0';

    char* p = args;
    while (*p && isspace((unsigned char)*p)) p++;
    char* end = p + strlen(p) - 1;
    while (end > p && isspace((unsigned char)*end)) *end-- = '\0';

    char* result = malloc(strlen(func_name) + strlen(args) + 16);
    snprintf(result, strlen(func_name) + strlen(args) + 16, "(%s %s)", func_name, args);

    free(func_name);
    free(args);
    return result;
  }

  if (strncmp(input, "tant_que ", 9) == 0) {
    const char* alors_pos = strstr(input, " alors ");
    if (alors_pos != NULL) {
      int cond_len = alors_pos - (input + 9);
      char* condition = malloc(cond_len + 1);
      strncpy(condition, input + 9, cond_len);
      condition[cond_len] = '\0';

      const char* body_content = alors_pos + 7;
      while (*body_content && isspace((unsigned char)*body_content)) body_content++;

      char* proc_cond = preprocess_infix(condition);
      char* proc_body;
      if (*body_content == '{') {
        int blen = strlen(body_content);
        char* inner = malloc(blen);
        strncpy(inner, body_content + 1, blen - 2);
        inner[blen - 2] = '\0';
        proc_body = preprocess_infix(inner);
        free(inner);
      } else {
        proc_body = preprocess_infix(body_content);
      }

      char* result = malloc(strlen(proc_cond) + strlen(proc_body) + 32);
      snprintf(result, strlen(proc_cond) + strlen(proc_body) + 32,
               "(tant_que {%s} {%s})", proc_cond, proc_body);

      free(condition);
      free(proc_cond);
      free(proc_body);
      return result;
    }
  }


  // Check les paternes graçe aux caractères*/ 
  // Merci à copilote pour la partie en dessous X) 
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
 
        // Start of then branch 
        const char* then_content = then_pos + 7;
        while (*then_content && isspace((unsigned char)*then_content)) then_content++;
        
        char* then_expr = malloc(4096);
        char* else_expr = malloc(4096);
        
        if (then_expr && else_expr) {
          then_expr[0] = '\0';
          else_expr[0] = '\0';
          
          int then_len = 0;
          
          // Extract then branch (handle both {expr} and bare expr)
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
            
            // Check for else after the closing brace
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
            // scan jusqu'à que on trouve le else
            if (else_pos) {
              then_len = else_pos - then_content;
              strncpy(then_expr, then_content, then_len);
              then_expr[then_len] = '\0';
              
              while (then_len > 0 && isspace((unsigned char)then_expr[then_len-1])) {
                then_expr[--then_len] = '\0';
              }
              
              const char* else_content = else_pos + 6;
              while (*else_content && isspace((unsigned char)*else_content)) else_content++;
              strcpy(else_expr, else_content);
            } else {
        
              strcpy(then_expr, then_content);
            }
          }

       
          // preprocesse 
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

  // Remplacer les terme qui ont des espaces avec des underscore _ 
  char* normalized = malloc(strlen(input) * 2 + 1);
  if (!normalized) return NULL;
  
  strcpy(normalized, input);
  char* search_replace[] = {
    "multiplier par", "multiplier_par",
    "multiplie par", "multiplie_par",
    "diviser par", "diviser_par",
    "divise par", "divise_par",
    "divisee par", "divisee_par",
  
    "est superieur a", "est_superieur_a",
    "est inferieur a", "est_inferieur_a",

    "sont superieur ou egale a", "sont_superieur_ou_egale_a",
    "sont superieur ou egaux a", "sont_superieur_ou_egaux_a",
    "est superieur ou egal a", "est_superieur_ou_egal_a",
    "est superieur ou egale a", "est_superieur_ou_egale_a",

    "sont inferieur ou egale a", "sont_inferieur_ou_egale_a",
    "sont inferieur ou egaux a", "sont_inferieur_ou_egaux_a",
    "est inferieur ou egal a", "est_inferieur_ou_egal_a",
    "est inferieur ou egale a", "est_inferieur_ou_egale_a",

    "n est pas egale a", "n_est_pas_egale_a",
    "n est pas egal a", "n_est_pas_egal_a",
    "n est pas different de", "n_est_pas_different_de",
    "ne sont pas egaux a", "ne_sont_pas_egaux_a",
    "ne sont pas egale a", "ne_sont_pas_egale_a",
    "ne sont pas egal a", "ne_sont_pas_egal_a",
    "ne sont pas egal", "ne_sont_pas_egal",
    "n est pas egal", "n_est_pas_egal",
    "n est pas egales a", "n_est_pas_egales_a",
    "n est pas egal", "n_est_pas_egal",
    "sont différents de", "sont_differents_de",
    "sont différents", "sont_differents",
    "est different de", "est_different_de",

    "sont egale a", "sont_egale_a",
    "sont egaux a", "sont_egaux_a",
    "est egale a", "est_egale_a",
    "est egal", "est_egal",
    "est egale", "est_egale",
    "tant que", "tant_que",
    NULL, NULL
  };
  
  for (int i = 0; search_replace[i] != NULL; i += 2) {
    char* pos = normalized;
    while ((pos = strstr(pos, search_replace[i])) != NULL) {
      int search_len = strlen(search_replace[i]);
      int replace_len = strlen(search_replace[i+1]);
      if (search_len == replace_len) {
        memcpy(pos, search_replace[i+1], replace_len);
        pos += replace_len;
      } else {
        pos[search_len/2] = '_';
        pos += search_len;
      }
    }
  }

  // Zone pour les règles arithmétiques habituelle

  char* input_copy = malloc(strlen(normalized) + 1);
  if (!input_copy) { free(normalized); return NULL; }
  strcpy(input_copy, normalized);
  
  char** tokens = NULL;
  int tokens_count = 0;
  char* saveptr = NULL;
  char* token = strtok_r(input_copy, " ", &saveptr);
  
  while (token != NULL) {
    char** tmp = realloc(tokens, sizeof(char*) * (tokens_count + 1));
    if (!tmp) { free(tokens); free(input_copy); free(normalized); return NULL; }
    tokens = tmp;
    tokens[tokens_count++] = token;
    token = strtok_r(NULL, " ", &saveptr);
  }

  if (tokens_count == 0) {
    free(tokens);
    free(input_copy); 
    free(normalized);
    return strdup("");
  }

  if (tokens_count < 3) {
    char* out = strdup(normalized);
    free(tokens);
    free(input_copy);
    free(normalized);
    return out;
  }

  int is_op = 0;
  for (size_t m = 0; m < sizeof(list_caractere)/sizeof(list_caractere[0]); m++) {
    char op_name[256];
    strcpy(op_name, tokens[1]);
    for (char* p = op_name; *p; p++) if (*p == '_') *p = ' ';
    if (strcmp(list_caractere[m].word, op_name) == 0) { is_op = 1; break; }
  }

if (!is_op) {
    int rest_is_expr = 0;
    if (tokens_count >= 4) {
      char op_check[256];
      strcpy(op_check, tokens[2]);
      for (char* p = op_check; *p; p++) if (*p == '_') *p = ' ';
      for (size_t m = 0; m < sizeof(list_caractere)/sizeof(list_caractere[0]); m++) {
        if (strcmp(list_caractere[m].word, op_check) == 0) { rest_is_expr = 1; break; }
      }
    }

    if (rest_is_expr) {
      size_t rest_len = 0;
      for (int i = 1; i < tokens_count; i++) rest_len += strlen(tokens[i]) + 1;
      char* rest = malloc(rest_len + 1);
      rest[0] = '\0';
      for (int i = 1; i < tokens_count; i++) {
        strcat(rest, tokens[i]);
        if (i < tokens_count - 1) strcat(rest, " ");
      }
      char* proc_rest = preprocess_infix(rest);
      char* result = malloc(strlen(tokens[0]) + strlen(proc_rest) + 4);
      snprintf(result, strlen(tokens[0]) + strlen(proc_rest) + 4,
               "(%s %s)", tokens[0], proc_rest);
      free(rest);
      free(proc_rest);
      free(tokens);
      free(input_copy);
      free(normalized);
      return result;
    }

    char* out = strdup(normalized);
    free(tokens);
    free(input_copy);
    free(normalized);
    return out;
  }

  /* Allocate token copies */
  char** tok = malloc(sizeof(char*) * tokens_count);
  for (int i = 0; i < tokens_count; i++) {
    tok[i] = malloc(strlen(tokens[i]) + 1);
    strcpy(tok[i], tokens[i]);
  }
  int cnt = tokens_count;

  for (int i = 0; i + 2 < cnt; i++) {
    char op_name[256];
    strcpy(op_name, tok[i + 1]);
    for (char* p = op_name; *p; p++) if (*p == '_') *p = ' ';

    if (is_comparison_op(op_name)) {
      combine_tokens(tok, &cnt, i, get_op_sym(op_name, list_caractere, sizeof(list_caractere)/sizeof(list_caractere[0])));
      i--;
    }
  }

  char* result = tok[0];
  free(tok);
  free(tokens);
  free(input_copy);
  free(normalized);
  return result;
}


// ---------------------------------------------------------------------------//

int main(int argc, char** argv) {

  puts("Chocolatine Version 0.9");
  puts("Ctrl+C pour quitter\n");
  
  // Création des parsers mpc pour le langage   
  Number = mpc_new("number");
  Bool   = mpc_new("bool");
  Symbol = mpc_new("symbol");
  String = mpc_new("string");
  Comment = mpc_new("comment");
  Sexpr  = mpc_new("sexpr");
  Qexpr  = mpc_new("qexpr");
  Expr   = mpc_new("expr");
  Lispy  = mpc_new("lispy");
  Call = mpc_new("call");
  
  // Définition de la grammaire en une seule chaîne (operator supprimé)
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number   : /-?[0-9]+(\\.[0-9]+)?/ ;                             \
      bool     : /vrai|faux/ ;                             \
      symbol   : /[a-zA-Z0-9_+\\-*\\/=<>!&\\\\]+/ ;        \
      comment :  /;[^\\r\\n]*/ ;                             \
      string  :  /\"[^\"]*\"|'[^']*'/ ;                     \
      sexpr    : '(' <expr>* ')' ;                        \
      qexpr    : '{' <expr>* '}' ;                        \
      expr     : <bool> | <number> | <symbol>  | <string> | <comment> | <sexpr> | <qexpr> | <call> ;\
      lispy    : /^/ <expr>* /$/ ;                        \
      call : <symbol> '(' <expr>* ')' ; \
    ",
    Number, Bool, Symbol, String, Comment, Sexpr, Qexpr, Call, Expr, Lispy);
  
  // Environnement global et enregistrement des fonctions natives 
  lenv* e = lenv_new();
  lenv_add_builtins(e);
  
  // Chargement des fichiers si jamais on utilise builtin_load
  if (argc >= 2) {
    for (int i = 1; i < argc; i++) {
      lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));
      lval* x = builtin_load(e, args);
      if (x->type == LVAL_ERR) { lval_println(x); }
      lval_del(x);
    }
  }
  
  // Boucle REPL : lire, prétraiter infix, évaluer, afficher 
  while (1) {
  
    char* input = readline("Chocolatine> ");
    add_history(input);
    
    // Prétraiter l'entrée pour convertir infix vers prefix
    char* preprocessed = preprocess_infix(input);
    free(input);
    
    mpc_result_t r;

    if (mpc_parse("<stdin>", preprocessed, Lispy, &r)) {
      // Lire le résulte puis après on free de la ram
      lval* x = lval_eval(e, lval_read(r.output));
      
      lval_println(x);
      
      lval_del(x);
      mpc_ast_delete(r.output);
      
    } else {    
      
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    free(preprocessed);
    
  }
  
  // Nettoyage final 
  lenv_del(e);
  
  mpc_cleanup(10, Number, Bool, Symbol, Comment, String, Sexpr, Qexpr, Call, Expr, Lispy);
  
  return 0;
}
