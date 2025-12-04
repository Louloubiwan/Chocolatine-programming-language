#include "mpc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>


#ifdef _WIN32

static char buffer[2048];

char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

void add_history(char* unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

/* Use operator string to see which operation to perform */
float eval_op(float x, char* op, float y) {
  if (strcmp(op, "+") == 0) { return x + y; }
  if (strcmp(op, "-") == 0) { return x - y; }
  if (strcmp(op, "*") == 0) { return x * y; }
  if (strcmp(op, "/") == 0) { return x / y; }
  if (strcmp(op, "max") == 0) {return (x > y ) ? x : y;}
  if (strcmp(op, "min") == 0) {return (x > y ) ? y : x;}
  return 0;
}

/*void float_to_int(float x, int output) {

  

  output = (int)x;
} */

float eval(mpc_ast_t* t) {
  
  /* If tagged as number return it directly. */ 
  if (strstr(t->tag, "number")) {
    return strtof(t->contents, NULL);
  }
  
  /* The operator is always second child. */
  char* op = t->children[2]->contents;
  
  /* We store the third child in `x` */
  float x = eval(t->children[1]);
  
  /* Iterate the remaining children and combining. */
  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }
  
  return x;  
}



int main(int argc, char** argv) {
  
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");
  
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number   : /[+-]?([0-9]*[.])?[0-9]+/ ;              \
      operator : '+' | '-' | '*' | '/' | \"max\" | \"min\";  \
      expr     : <number> | '(' <expr>+ <operator> <expr>+ ')' ;  \
      lispy    : /^/ <expr>+ <operator> <expr>+ /$/ ;             \
    ",
    Number, Operator, Expr, Lispy);
  
  puts("Chocolatine Version 0.1");
  puts("Press Ctrl+c to Exit\n");
  
  while (1) {
  
    char* input = readline("Chocolatine> ");
    add_history(input);
    
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      
       float result = eval(r.output);
      /* If result is very close to an integer, print as int; else print as float */
      if (fabsf(result - roundf(result)) < 1e-6f) {
        printf("%d\n", (int)roundf(result));
      } else {
        printf("%g\n", result);
      }
      
      mpc_ast_delete(r.output);
      
    } else {    
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    
    free(input);
    
  }
  
  mpc_cleanup(4, Number, Operator, Expr, Lispy);
  
  return 0;
}