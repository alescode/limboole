#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/*------------------------------------------------------------------------*/

#include "limmat/limmat.h"

/*------------------------------------------------------------------------*/

char *limboole_id = "$Id: limboole.c,v 1.15 2002/11/07 07:12:07 biere Exp $";

/*------------------------------------------------------------------------*/
/* These are the node types we support.  They are ordered in decreasing
 * priority: if a parent with type t1 has a child with type t2 and t1 > t2,
 * then pretty printing the parent requires parentheses.  See 'pp_aux' for
 * more details.
 */
enum Type
{
  VAR = 0,
  LP = 1,
  RP = 2,
  NOT = 3,
  AND = 4,
  OR = 5,
  IMPLIES = 6,
  IFF = 7,
  DONE = 8,
  ERROR = 9,
};

/*------------------------------------------------------------------------*/

typedef enum Type Type;
typedef struct Node Node;
typedef union Data Data;

/*------------------------------------------------------------------------*/

union Data
{
  char *as_name;		/* variable data */
  Node *as_child[2];		/* operator data */
};

/*------------------------------------------------------------------------*/

struct Node
{
  Type type;
  int idx;			/* tsetin index */
  Node *next;			/* collision chain in hash table */
  Node *next_inserted;		/* chronological list of hash table */
  Data data;
};

/*------------------------------------------------------------------------*/

typedef struct Mgr Mgr;

struct Mgr
{
  unsigned nodes_size;
  unsigned nodes_count;
  int idx;
  Node **nodes;
  Node *first;
  Node *last;
  Node *root;
  char *buffer;
  char *name;
  unsigned buffer_size;
  unsigned buffer_count;
  int saved_char;
  int saved_char_is_valid;
  int last_y;
  int verbose;
  unsigned x;
  unsigned y;
  Limmat *limmat;
  FILE *in;
  FILE *log;
  FILE *out;
  int close_in;
  int close_log;
  int close_out;
  Type token;
  unsigned token_x;
  unsigned token_y;
  Node **idx2node;
  int check_satisfiability;
  int dump;
};

/*------------------------------------------------------------------------*/

static unsigned
hash_var (Mgr * mgr, const char *name)
{
  unsigned res, tmp;
  const char *p;

  res = 0;

  for (p = name; *p; p++)
    {
      tmp = res & 0xf0000000;
      res <<= 4;
      res += *p;
      if (tmp)
	res ^= (tmp >> 28);
    }

  res &= (mgr->nodes_size - 1);
  assert (res < mgr->nodes_size);

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
hash_op (Mgr * mgr, Type type, Node * c0, Node * c1)
{
  unsigned res;

  res = (unsigned) type;
  res += 4017271 * (unsigned) c0;
  res += 70200511 * (unsigned) c1;

  res &= (mgr->nodes_size - 1);
  assert (res < mgr->nodes_size);

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
hash (Mgr * mgr, Type type, void *c0, Node * c1)
{
  if (type == VAR)
    return hash_var (mgr, (char *) c0);
  else
    return hash_op (mgr, type, c0, c1);
}

/*------------------------------------------------------------------------*/

static int
eq_var (Node * n, const char *str)
{
  return n->type == VAR && !strcmp (n->data.as_name, str);
}

/*------------------------------------------------------------------------*/

static int
eq_op (Node * n, Type type, Node * c0, Node * c1)
{
  return n->type == type && n->data.as_child[0] == c0
    && n->data.as_child[1] == c1;
}

/*------------------------------------------------------------------------*/

static int
eq (Node * n, Type type, void *c0, Node * c1)
{
  if (type == VAR)
    return eq_var (n, (char *) c0);
  else
    return eq_op (n, type, c0, c1);
}

/*------------------------------------------------------------------------*/

static Node **
find (Mgr * mgr, Type type, void *c0, Node * c1)
{
  Node **p, *n;
  unsigned h;

  h = hash (mgr, type, c0, c1);
  for (p = mgr->nodes + h; (n = *p); p = &n->next)
    if (eq (n, type, c0, c1))
      break;

  return p;
}

/*------------------------------------------------------------------------*/

static void
enlarge_nodes (Mgr * mgr)
{
  Node **old_nodes, *p, *next;
  unsigned old_nodes_size, h, i;

  old_nodes = mgr->nodes;
  old_nodes_size = mgr->nodes_size;
  mgr->nodes_size *= 2;
  mgr->nodes = (Node **) calloc (mgr->nodes_size, sizeof (Node *));

  for (i = 0; i < old_nodes_size; i++)
    {
      for (p = old_nodes[i]; p; p = next)
	{
	  next = p->next;
	  if (p->type == VAR)
	    h = hash_var (mgr, p->data.as_name);
	  else
	    h =
	      hash_op (mgr, p->type, p->data.as_child[0],
		       p->data.as_child[1]);
	  p->next = mgr->nodes[h];
	  mgr->nodes[h] = p;
	}
    }

  free (old_nodes);
}

/*------------------------------------------------------------------------*/

static void
insert (Mgr * mgr, Node * node)
{
  if (mgr->last)
    mgr->last->next_inserted = node;
  else
    mgr->first = node;
  mgr->last = node;
  mgr->nodes_count++;
}

/*------------------------------------------------------------------------*/

static Node *
var (Mgr * mgr, const char *str)
{
  Node **p, *n;

  if (mgr->nodes_size <= mgr->nodes_count)
    enlarge_nodes (mgr);

  p = find (mgr, VAR, (void *) str, 0);
  n = *p;
  if (!n)
    {
      n = (Node *) malloc (sizeof (*n));
      memset (n, 0, sizeof (*n));
      n->type = VAR;
      n->data.as_name = strdup (str);

      *p = n;
      insert (mgr, n);
    }

  return n;
}

/*------------------------------------------------------------------------*/

static Node *
op (Mgr * mgr, Type type, Node * c0, Node * c1)
{
  Node **p, *n;

  if (mgr->nodes_size <= mgr->nodes_count)
    enlarge_nodes (mgr);

  p = find (mgr, type, c0, c1);
  n = *p;
  if (!n)
    {
      n = (Node *) malloc (sizeof (*n));
      memset (n, 0, sizeof (*n));
      n->type = type;
      n->data.as_child[0] = c0;
      n->data.as_child[1] = c1;

      *p = n;
      insert (mgr, n);
    }

  return n;
}

/*------------------------------------------------------------------------*/

static Mgr *
init (void)
{
  Mgr *res;

  res = (Mgr *) malloc (sizeof (*res));
  memset (res, 0, sizeof (*res));
  res->nodes_size = 2;
  res->nodes = (Node **) calloc (res->nodes_size, sizeof (Node *));
  res->buffer_size = 2;
  res->buffer = (char *) malloc (res->buffer_size);
  res->in = stdin;
  res->log = stderr;
  res->out = stdout;

  return res;
}

/*------------------------------------------------------------------------*/

static void
connect_solver (Mgr * mgr)
{
  assert (!mgr->limmat);
  mgr->limmat = new_Limmat (mgr->verbose ? mgr->log : 0);
}

/*------------------------------------------------------------------------*/

static void
release (Mgr * mgr)
{
  Node *p, *next;

  if (mgr->limmat)
    delete_Limmat (mgr->limmat);

  for (p = mgr->first; p; p = next)
    {
      next = p->next_inserted;
      if (p->type == VAR)
	free (p->data.as_name);
      free (p);
    }


  if (mgr->close_in)
    fclose (mgr->in);
  if (mgr->close_out)
    fclose (mgr->out);
  if (mgr->close_log)
    fclose (mgr->log);

  free (mgr->idx2node);
  free (mgr->nodes);
  free (mgr->buffer);
  free (mgr);
}

/*------------------------------------------------------------------------*/

static void
print_token (Mgr * mgr)
{
  switch (mgr->token)
    {
    case VAR:
      fputs (mgr->buffer, mgr->log);
      break;
    case LP:
      fputc ('(', mgr->log);
      break;
    case RP:
      fputc (')', mgr->log);
      break;
    case NOT:
      fputc ('!', mgr->log);
      break;
    case AND:
      fputc ('&', mgr->log);
      break;
    case OR:
      fputc ('|', mgr->log);
      break;
    case IMPLIES:
      fputs ("->", mgr->log);
      break;
    case IFF:
      fputs ("<->", mgr->log);
      break;
    default:
      assert (mgr->token == DONE);
      fputs ("EOF", mgr->log);
      break;
    }
}

/*------------------------------------------------------------------------*/

static void
parse_error (Mgr * mgr, const char *fmt, ...)
{
  va_list ap;
  char *name;

  name = mgr->name ? mgr->name : "<stdin>";
  fprintf (mgr->log, "%s:%u:%u: ", name, mgr->token_x + 1, mgr->token_y);
  if (mgr->token == ERROR)
    fputs ("scan error: ", mgr->log);
  else
    {
      fputs ("parse error at '", mgr->log);
      print_token (mgr);
      fputs ("' ", mgr->log);
    }
  va_start (ap, fmt);
  vfprintf (mgr->log, fmt, ap);
  va_end (ap);
  fputc ('\n', mgr->log);
}

/*------------------------------------------------------------------------*/

static int
is_var_letter (int ch)
{
  if (isalnum (ch))
    return 1;

  switch (ch)
    {
    case '-':
    case '_':
    case '.':
    case '[':
    case ']':
    case '$':
    case '@':
      return 1;

    default:
      return 0;
    }
}

/*------------------------------------------------------------------------*/

static void
enlarge_buffer (Mgr * mgr)
{
  mgr->buffer_size *= 2;
  mgr->buffer = (char *) realloc (mgr->buffer, mgr->buffer_size);
}

/*------------------------------------------------------------------------*/

static int
next_char (Mgr * mgr)
{
  int res;

  mgr->last_y = mgr->y;

  if (mgr->saved_char_is_valid)
    {
      mgr->saved_char_is_valid = 0;
      res = mgr->saved_char;
    }
  else
    res = fgetc (mgr->in);

  if (res == '\n')
    {
      mgr->x++;
      mgr->y = 0;
    }
  else
    mgr->y++;

  return res;
}

/*------------------------------------------------------------------------*/

static void
unget_char (Mgr * mgr, int ch)
{
  assert (!mgr->saved_char_is_valid);

  mgr->saved_char_is_valid = 1;
  mgr->saved_char = ch;

  if (ch == '\n')
    {
      mgr->x--;
      mgr->y = mgr->last_y;
    }
  else
    mgr->y--;
}

/*------------------------------------------------------------------------*/

static void
next_token (Mgr * mgr)
{
  int ch;

  mgr->token = ERROR;
  ch = next_char (mgr);

RESTART_NEXT_TOKEN:

  while (isspace ((int) ch))
    ch = next_char (mgr);

  if (ch == '%')
    {
      while ((ch = next_char (mgr)) != '\n' && ch != EOF)
	;

      goto RESTART_NEXT_TOKEN;
    }

  mgr->token_x = mgr->x;
  mgr->token_y = mgr->y;

  if (ch == EOF)
    mgr->token = DONE;
  else if (ch == '<')
    {
      if (next_char (mgr) != '-')
	parse_error (mgr, "expected '-' after '<'");
      else if (next_char (mgr) != '>')
	parse_error (mgr, "expected '>' after '-'");
      else
	mgr->token = IFF;
    }
  else if (ch == '-')
    {
      if (next_char (mgr) != '>')
	parse_error (mgr, "expected '>' after '-'");
      else
	mgr->token = IMPLIES;
    }
  else if (ch == '&')
    {
      mgr->token = AND;
    }
  else if (ch == '|')
    {
      mgr->token = OR;
    }
  else if (ch == '!' || ch == '~')
    {
      mgr->token = NOT;
    }
  else if (ch == '(')
    {
      mgr->token = LP;
    }
  else if (ch == ')')
    {
      mgr->token = RP;
    }
  else if (is_var_letter (ch))
    {
      mgr->buffer_count = 0;

      while (is_var_letter (ch))
	{
	  if (mgr->buffer_size <= mgr->buffer_count + 1)
	    enlarge_buffer (mgr);

	  mgr->buffer[mgr->buffer_count++] = ch;
	  ch = next_char (mgr);
	}

      unget_char (mgr, ch);
      mgr->buffer[mgr->buffer_count] = 0;

      if (mgr->buffer[mgr->buffer_count - 1] == '-')
	parse_error (mgr, "variable '%s' ends with '-'", mgr->buffer);
      else
	mgr->token = VAR;
    }
  else
    parse_error (mgr, "invalid character '%c'", ch);
}

/*------------------------------------------------------------------------*/

static Node *parse_expr (Mgr *);

/*------------------------------------------------------------------------*/

static Node *
parse_basic (Mgr * mgr)
{
  Node *child;
  Node *res;

  res = 0;

  if (mgr->token == LP)
    {
      next_token (mgr);
      child = parse_expr (mgr);
      if (mgr->token != RP)
	{
	  if (mgr->token != ERROR)
	    parse_error (mgr, "expected ')'");
	}
      else
	res = child;
      next_token (mgr);
    }
  else if (mgr->token == VAR)
    {
      res = var (mgr, mgr->buffer);
      next_token (mgr);
    }
  else if (mgr->token != ERROR)
    parse_error (mgr, "expected variable or '('");

  return res;
}

/*------------------------------------------------------------------------*/

static Node *
parse_not (Mgr * mgr)
{
  Node *child, *res;

  if (mgr->token == NOT)
    {
      next_token (mgr);
      child = parse_not (mgr);
      if (child)
	res = op (mgr, NOT, child, 0);
      else
	res = 0;
    }
  else
    res = parse_basic (mgr);

  return res;
}

/*------------------------------------------------------------------------*/

static Node *
parse_associative_op (Mgr * mgr, Type type, Node * (*lower) (Mgr *))
{
  Node *res, *child;
  int done;

  res = 0;
  done = 0;

  do
    {
      child = lower (mgr);
      if (child)
	{
	  res = res ? op (mgr, type, res, child) : child;
	  if (mgr->token == type)
	    next_token (mgr);
	  else
	    done = 1;
	}
      else
	res = 0;
    }
  while (res && !done);

  return res;
}


/*------------------------------------------------------------------------*/

static Node *
parse_and (Mgr * mgr)
{
  return parse_associative_op (mgr, AND, parse_not);
}

/*------------------------------------------------------------------------*/

static Node *
parse_or (Mgr * mgr)
{
  return parse_associative_op (mgr, OR, parse_and);
}

/*------------------------------------------------------------------------*/

static Node *
parse_implies (Mgr * mgr)
{
  Node *l, *r;

  if (!(l = parse_or (mgr)))
    return 0;
  if (mgr->token != IMPLIES)
    return l;
  next_token (mgr);
  if (!(r = parse_or (mgr)))
    return 0;

  return op (mgr, IMPLIES, l, r);
}

/*------------------------------------------------------------------------*/

static Node *
parse_iff (Mgr * mgr)
{
  return parse_associative_op (mgr, IFF, parse_implies);
}

/*------------------------------------------------------------------------*/

static Node *
parse_expr (Mgr * mgr)
{
  return parse_iff (mgr);
}

/*------------------------------------------------------------------------*/

static int
parse (Mgr * mgr)
{
  next_token (mgr);

  if (mgr->token == ERROR)
    return 0;

  if (!(mgr->root = parse_expr (mgr)))
    return 0;

  if (mgr->token == DONE)
    return 1;

  if (mgr->token != ERROR)
    parse_error (mgr, "expected operator or EOF");

  return 0;
}

/*------------------------------------------------------------------------*/

static void
unit_clause (Mgr * mgr, int a)
{
  int clause[2];

  clause[0] = a;
  clause[1] = 0;

  add_Limmat (mgr->limmat, clause);

  if (mgr->dump)
    fprintf (mgr->out, "%d 0\n", a);
}

/*------------------------------------------------------------------------*/

static void
binary_clause (Mgr * mgr, int a, int b)
{
  int clause[3];

  clause[0] = a;
  clause[1] = b;
  clause[2] = 0;

  add_Limmat (mgr->limmat, clause);

  if (mgr->dump)
    fprintf (mgr->out, "%d %d 0\n", a, b);
}

/*------------------------------------------------------------------------*/

static void
ternary_clause (Mgr * mgr, int a, int b, int c)
{
  int clause[4];

  clause[0] = a;
  clause[1] = b;
  clause[2] = c;
  clause[3] = 0;

  add_Limmat (mgr->limmat, clause);

  if (mgr->dump)
    fprintf (mgr->out, "%d %d %d 0\n", a, b, c);
}

/*------------------------------------------------------------------------*/

static void
tsetin (Mgr * mgr)
{
  int num_clauses;
  int sign;
  Node *p;

  num_clauses = 0;

  for (p = mgr->first; p; p = p->next_inserted)
    {
      p->idx = ++mgr->idx;

      if (mgr->dump && p->type == VAR)
	fprintf (mgr->out, "c %d %s\n", p->idx, p->data.as_name);

      switch (p->type)
	{
	case IFF:
	  num_clauses += 4;
	  break;
	case OR:
	case AND:
	case IMPLIES:
	  num_clauses += 3;
	  break;
	case NOT:
	  num_clauses += 2;
	  break;
	default:
	  assert (p->type == VAR);
	  break;
	}
    }

  mgr->idx2node = (Node **) calloc (mgr->idx + 1, sizeof (Node *));
  for (p = mgr->first; p; p = p->next_inserted)
    mgr->idx2node[p->idx] = p;

  if (mgr->dump)
    fprintf (mgr->out, "p cnf %d %u\n", mgr->idx, num_clauses + 1);

  for (p = mgr->first; p; p = p->next_inserted)
    {
      switch (p->type)
	{
	case IFF:
	  ternary_clause (mgr, p->idx,
			  -p->data.as_child[0]->idx,
			  -p->data.as_child[1]->idx);
	  ternary_clause (mgr, p->idx, p->data.as_child[0]->idx,
			  p->data.as_child[1]->idx);
	  ternary_clause (mgr, -p->idx, -p->data.as_child[0]->idx,
			  p->data.as_child[1]->idx);
	  ternary_clause (mgr, -p->idx, p->data.as_child[0]->idx,
			  -p->data.as_child[1]->idx);
	  break;
	case IMPLIES:
	  binary_clause (mgr, p->idx, p->data.as_child[0]->idx);
	  binary_clause (mgr, p->idx, -p->data.as_child[1]->idx);
	  ternary_clause (mgr, -p->idx,
			  -p->data.as_child[0]->idx,
			  p->data.as_child[1]->idx);
	  break;
	case OR:
	  binary_clause (mgr, p->idx, -p->data.as_child[0]->idx);
	  binary_clause (mgr, p->idx, -p->data.as_child[1]->idx);
	  ternary_clause (mgr, -p->idx,
			  p->data.as_child[0]->idx, p->data.as_child[1]->idx);
	  break;
	case AND:
	  binary_clause (mgr, -p->idx, p->data.as_child[0]->idx);
	  binary_clause (mgr, -p->idx, p->data.as_child[1]->idx);
	  ternary_clause (mgr, p->idx,
			  -p->data.as_child[0]->idx,
			  -p->data.as_child[1]->idx);
	  break;
	case NOT:
	  binary_clause (mgr, p->idx, p->data.as_child[0]->idx);
	  binary_clause (mgr, -p->idx, -p->data.as_child[0]->idx);
	  break;
	default:
	  assert (p->type == VAR);
	  break;
	}
    }

  assert (mgr->root);

  sign = (mgr->check_satisfiability) ? 1 : -1;
  unit_clause (mgr, sign * mgr->root->idx);
}

/*------------------------------------------------------------------------*/

static void
pp_aux (Mgr * mgr, Node * node, Type outer)
{
  int le, lt;

  le = outer <= node->type;
  lt = outer < node->type;

  switch (node->type)
    {
    case NOT:
      fputc ('!', mgr->out);
      pp_aux (mgr, node->data.as_child[0], node->type);
      break;
    case IMPLIES:
    case IFF:
      if (le)
	fputc ('(', mgr->out);
      pp_aux (mgr, node->data.as_child[0], node->type);
      fputs (node->type == IFF ? " <-> " : " -> ", mgr->out);
      pp_aux (mgr, node->data.as_child[1], node->type);
      if (le)
	fputc (')', mgr->out);
      break;

    case OR:
    case AND:
      if (lt)
	fputc ('(', mgr->out);
      pp_aux (mgr, node->data.as_child[0], node->type);
      fputs (node->type == OR ? " | " : " & ", mgr->out);
      pp_aux (mgr, node->data.as_child[1], node->type);
      if (lt)
	fputc (')', mgr->out);
      break;

    default:
      assert (node->type == VAR);
      fprintf (mgr->out, "%s", node->data.as_name);
      break;
    }
}

/*------------------------------------------------------------------------*/

static void
pp_and (Mgr * mgr, Node * node)
{
  if (node->type == AND)
    {
      pp_and (mgr, node->data.as_child[0]);
      fprintf (mgr->out, "\n&\n");
      pp_and (mgr, node->data.as_child[1]);
    }
  else
    pp_aux (mgr, node, AND);
}

/*------------------------------------------------------------------------*/

static void
pp_or (Mgr * mgr, Node * node)
{
  if (node->type == OR)
    {
      pp_or (mgr, node->data.as_child[0]);
      fprintf (mgr->out, "\n|\n");
      pp_or (mgr, node->data.as_child[1]);
    }
  else
    pp_aux (mgr, node, OR);
}

/*------------------------------------------------------------------------*/

static void
pp_and_or (Mgr * mgr, Node * node, Type outer)
{
  assert (outer > AND);
  assert (outer > OR);

  if (node->type == AND)
    pp_and (mgr, node);
  else if (node->type == OR)
    pp_or (mgr, node);
  else
    pp_aux (mgr, node, outer);
}

/*------------------------------------------------------------------------*/

static void
pp_iff_implies (Mgr * mgr, Node * node, Type outer)
{
  if (node->type == IFF || node->type == IMPLIES)
    {
      pp_and_or (mgr, node->data.as_child[0], node->type);
      fprintf (mgr->out, "\n%s\n", node->type == IFF ? "<->" : "->");
      pp_and_or (mgr, node->data.as_child[1], node->type);
    }
  else
    pp_and_or (mgr, node, outer);
}

/*------------------------------------------------------------------------*/

static void
pp (Mgr * mgr)
{
  assert (mgr->root);
  pp_iff_implies (mgr, mgr->root, DONE);
  fputc ('\n', mgr->out);
}

/*------------------------------------------------------------------------*/

static void
print_assignment (Mgr * mgr, const int *assignment)
{
  const int *p;
  Node *n;
  int val;
  int idx;

  for (p = assignment; *p; p++)
    {
      idx = *p;
      if (idx < 0)
	{
	  idx = -idx;
	  val = 0;
	}
      else
	val = 1;

      assert (idx > 0);
      assert (idx <= mgr->idx);

      n = mgr->idx2node[idx];
      if (n->type == VAR)
	fprintf (mgr->out, "%s = %d\n", n->data.as_name, val);
    }
}

/*------------------------------------------------------------------------*/

#define USAGE \
"usage: limboole [ <option> ... ]\n" \
"\n" \
"  -h             print this command line summary and exit\n" \
"  --version      print the version and exit\n" \
"  -v             increase verbosity\n" \
"  -p             pretty print input formula only\n" \
"  -d             dump generated CNF only\n" \
"  -s             check satisfiability (default is to check validity)\n" \
"  -m <max-dec>   maximal decision bound (default unbounded)\n" \
"  -o <out-file>  set output file (default <stdout>)\n" \
"  -l <log-file>  set log file (default <stderr>)\n" \
"  <in-file>      input file (default <stdin>)\n"

/*------------------------------------------------------------------------*/

int
limboole (int argc, char **argv)
{
  const int *assignment;
  int max_decisions;
  int pretty_print;
  FILE *file;
  int error;
  Mgr *mgr;
  int done;
  int res;
  int i;

  done = 0;
  error = 0;
  pretty_print = 0;
  max_decisions = -1;

  mgr = init ();

  for (i = 1; !done && !error && i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fprintf (mgr->out, USAGE);
	  done = 1;
	}
      else if (!strcmp (argv[i], "--version"))
	{
	  fprintf (mgr->out, "%s\n", VERSION);
	  done = 1;
	}
      else if (!strcmp (argv[i], "-v"))
	{
	  mgr->verbose += 1;
	}
      else if (!strcmp (argv[i], "-p"))
	{
	  pretty_print = 1;
	}
      else if (!strcmp (argv[i], "-d"))
	{
	  mgr->dump = 1;
	}
      else if (!strcmp (argv[i], "-s"))
	{
	  mgr->check_satisfiability = 1;
	}
      else if (!strcmp (argv[i], "-m"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (mgr->log, "*** argument to '-m' missing (try '-h')\n");
	      error = 1;
	    }
	  else
	    max_decisions = atoi (argv[++i]);
	}
      else if (!strcmp (argv[i], "-o"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (mgr->log, "*** argument to '-o' missing (try '-h')\n");
	      error = 1;
	    }
	  else if (!(file = fopen (argv[++i], "w")))
	    {
	      fprintf (mgr->log, "*** could not write '%s'\n", argv[i]);
	      error = 1;
	    }
	  else if (mgr->close_out)
	    {
	      /* We moved this down for coverage in testing purposes */
	      fclose (file);
	      fprintf (mgr->log, "*** '-o' specified twice (try '-h')\n");
	      error = 1;
	    }
	  else
	    {
	      mgr->out = file;
	      mgr->close_out = 1;
	    }
	}
      else if (!strcmp (argv[i], "-l"))
	{
	  if (i == argc - 1)
	    {
	      fprintf (mgr->log, "*** argument to '-l' missing (try '-h')\n");
	      error = 1;
	    }
	  else if (!(file = fopen (argv[++i], "a")))
	    {
	      fprintf (mgr->log, "*** could not append to '%s'\n", argv[i]);
	      error = 1;
	    }
	  else if (mgr->close_log)
	    {
	      /* We moved this down for coverage in testing purposes */
	      fclose (file);
	      fprintf (mgr->log, "*** '-l' specified twice (try '-h')\n");
	      error = 1;
	    }
	  else
	    {
	      mgr->log = file;
	      mgr->close_log = 1;
	    }
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (mgr->log,
		   "*** invalid command line option '%s' (try '-h')\n",
		   argv[i]);
	  error = 1;
	}
      else if (mgr->close_in)
	{
	  fprintf (mgr->log,
		   "*** can not read more than two files (try '-h')\n");
	  error = 1;
	}
      else if (!(file = fopen (argv[i], "r")))
	{
	  fprintf (mgr->log, "*** could not read '%s'\n", argv[i]);
	  error = 1;
	}
      else
	{
	  mgr->in = file;
	  mgr->name = argv[i];
	  mgr->close_in = 1;
	}
    }

  if (!error && !done)
    {
      error = !parse (mgr);

      if (!error)
	{
	  if (pretty_print)
	    pp (mgr);
	  else
	    {
	      connect_solver (mgr);
	      tsetin (mgr);
	      if (!mgr->dump)
		{
		  if (mgr->verbose)
		    set_log_Limmat (mgr->limmat, mgr->log);

		  res = sat_Limmat (mgr->limmat, max_decisions);

		  if (res < 0)
		    {
		      fprintf (mgr->out, "%% RESOURCES EXHAUSTED\n");
		      error = 1;
		    }
		  else if (res == 1)
		    {
		      if (mgr->check_satisfiability)
			fprintf (mgr->out,
				 "%% SATISFIABLE formula"
				 " (satisfying assignment follows)\n");
		      else
			fprintf (mgr->out,
				 "%% INVALID formula"
				 " (falsifying assignment follows)\n");

		      assignment = assignment_Limmat (mgr->limmat);
		      print_assignment (mgr, assignment);
		    }
		  else
		    {
		      if (mgr->check_satisfiability)
			fprintf (mgr->out, "%% UNSATISFIABLE formula\n");
		      else
			fprintf (mgr->out, "%% VALID formula\n");
		    }
		}
	    }
	}
    }

  release (mgr);

  return error != 0;
}
