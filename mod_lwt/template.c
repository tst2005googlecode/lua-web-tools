/*
 * Provides the mod_lwt template functions. See LICENSE for license terms.
 */

#include <ctype.h>
#include <apr_hash.h>
#include <apr_strings.h>
#include <http_protocol.h>
#include <lauxlib.h>
#include "util.h"
#include "template.h"

/*
 * Parser record.
 */
typedef struct parser_rec {
	const char *filename;
	lua_State *L;
	int flags;
	apr_pool_t *pool;
	char *buf;
	char *begin;
	char *pos;
	apr_array_header_t *t;
	apr_array_header_t *b;
	const char *err;
} parser_rec;


/*
 * Render record.
 */
typedef struct render_rec {
	apr_array_header_t *t;
	lua_State *L;
	apr_pool_t *pool;
	FILE *f;
	int errfunc;
	apr_hash_t *templates;
	int depth;
	const char *err;
} render_rec;

/**
 * Template node.
 */
typedef struct template_node_t {
        int type;
        union {
		struct {
			int jump_next;
		};
                struct {
                        const char *if_cond;
			int if_index;
                        int if_next;
                };
                struct {
                        const char *for_init_in;
			int for_init_index;
		};
		struct {
                        apr_array_header_t *for_next_names;
                        int for_next_next;
                };
		struct {
			apr_array_header_t *set_names;
			const char *set_expressions;
			int set_index;
		};
		struct {
			const char *include_filename;
			int include_index;
			const char *include_flags;
		};
                struct {
                        char *sub_exp;
			int sub_index;
                        int sub_flags;
                };
                struct {
                        const char *raw_str;
                        int raw_len;
                };
        };
} template_node_t;

/*
 * Block.
 */
typedef struct block_t {
	int type;
	union {
		struct {
			int if_start;
			int if_last;
			int if_cnt;
		};
		struct {
			int for_start;
		};
	};
} block_t;

/*
 * Node types.
 */
#define TEMPLATE_TJUMP 1
#define TEMPLATE_TIF 2
#define TEMPLATE_TFOR_INIT 3
#define TEMPLATE_TFOR_NEXT 4
#define TEMPLATE_TSET 5
#define TEMPLATE_TINCLUDE 6
#define TEMPLATE_TSUB 7
#define TEMPLATE_TRAW 8

/*
 * Element states.
 */
#define TEMPLATE_SOPEN 1
#define TEMPLATE_SCLOSE 2

/*
 * Processing flags.
 */
#define TEMPLATE_FPARSE 1
#define TEMPLATE_FESCXML 2
#define TEMPLATE_FESCURL 4
#define TEMPLATE_FSUPNIL 8
#define TEMPLATE_FSUPERR 16
#define TEMPLATE_DEFAULT_FLAGS "px"

/*
 * Limits.
 */
#define TEMPLATE_MAX_DEPTH 8

/*
 * Prototype for recusive includes.
 */
static apr_status_t parse_template (parser_rec *p); 

/*
 * Retruns a parse error.
 */
static apr_status_t parse_error (parser_rec *p, const char *msg) {
	char *pos;
	int linenumber;

	pos = p->buf;
	linenumber = 1;
	while (pos < p->pos) {
		switch (*pos) {
		case '\n':
			linenumber++;
			pos++;
			break;

		case '\r':
			linenumber++;
			pos++;
			if (*pos == '\n') {
				pos++;
			}
			break;

		default:
			pos++;
		}
	}

	p->err = apr_psprintf(p->pool, "%s, line %d: %s", p->filename,
			linenumber, msg);

	return APR_EGENERAL;
}

/*
 * Returns a runtime error.
 */
static apr_status_t runtime_error (render_rec *d) {
	const char *error_message;

	error_message = lua_tostring(d->L, -1);
	if (error_message == NULL) {
		error_message = "(NULL)";
	}
	d->err = apr_psprintf(d->pool, "Lua runtime error: %s", error_message);

	return APR_EGENERAL;
}

/*
 * Parses a flags string.
 */
static int parse_flags (const char *flags) {
	int value = 0;
	while (*flags != '\0') {
		switch (*flags) {
		case 'p':
			value |= TEMPLATE_FPARSE;
			break;

		case 'x':
			value |= TEMPLATE_FESCXML;
			break;

		case 'u':
			value |= TEMPLATE_FESCURL;
			break;

		case 'n':
			value |= TEMPLATE_FSUPNIL;
			break;

		case 'e':
			value |= TEMPLATE_FSUPERR;
			break;
		}
		flags++;
	}
	return value;
}

/*
 * Unescapes XML in place.
 */
static void unescape_xml (char *str) {
	char *r, *w;

	r = str;
	w = str;
	do {
		if (*r == '&') {
			if (r[1] == 'q' && r[2] == 'u' && r[3] == 'o'
					&& r[4] == 't' && r[5] == ';') {
				*w = '"';
				r += 6;
				w++;
			} else if (r[1] == 'l' && r[2] == 't' && r[3] == ';') {
				*w = '<';
				r += 4;
				w++;
			} else if (r[1] == 'g' && r[2] == 't' && r[3] == ';') {
				*w = '>';
				r += 4;
				w++;
			} else if (r[1] == 'a' && r[2] == 'm' && r[3] == 'p'
					&& r[4] == ';') {
				*w = '&';
				r += 5;
				w++;
			} else {
				*w = *r;
				r++;
				w++;
			}	
		} else {
			*w = *r;
			r++;
			w++;
		}
	} while (*r != '\0');
	*w = '\0';
}

/*
 * Compiles an expression.
 */
static apr_status_t compile_exp (parser_rec *p, const char *exp,
		int *index) {
	const char *chunk;

	chunk = apr_pstrcat(p->pool, "return ", exp, NULL);
	switch (luaL_loadbuffer(p->L, chunk, strlen(chunk), exp)) {
	case LUA_ERRSYNTAX:
	case LUA_ERRMEM:
		return parse_error(p, lua_tostring(p->L, -1));
	}
	*index = luaL_ref(p->L, LUA_REGISTRYINDEX);
	
	return APR_SUCCESS;
}

/*
 * Evaluates an expression.
 */
static apr_status_t evaluate_exp (render_rec *d, int index, int nret) {
	lua_rawgeti(d->L, LUA_REGISTRYINDEX, index);
	switch (lua_pcall(d->L, 0, nret, d->errfunc)) {
	case LUA_ERRRUN:
	case LUA_ERRMEM:
	case LUA_ERRERR:
		return runtime_error(d);
	}

	return APR_SUCCESS;
} 

/*
 * Evalutes an expression as a string.
 */
static apr_status_t evaluate_exp_str (render_rec *d, int index) {
	apr_status_t status;

	if ((status = evaluate_exp(d, index, 1)) != APR_SUCCESS) {
		return status;
	}
	if (!lua_isstring(d->L, -1)) {
		lua_pushfstring(d->L, "(%s)", luaL_typename(d->L, -1));
		lua_replace(d->L, -2);
	}

	return APR_SUCCESS;
}
		
/*
 * Element processor type.
 */
typedef apr_status_t (*element_processor) (parser_rec *p, const char *element,
		int states, apr_table_t *attrs); 

/*
 * Processes a 'if' element.
 */
static apr_status_t process_if (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
	block_t *block;
	template_node_t *n;
	apr_status_t status;

	if ((states & TEMPLATE_SOPEN) != 0) {
		block = (block_t *) apr_array_push(p->b);
		block->type = TEMPLATE_TIF;
		block->if_start = p->t->nelts;	
		block->if_last = p->t->nelts;
		block->if_cnt = 0;

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TIF;
		n->if_cond = apr_table_get(attrs, "cond");
		if (n->if_cond == NULL) {
			return parse_error(p, "missing attribute 'cond'");
		}
		if ((status = compile_exp(p, n->if_cond, &n->if_index))
				!= APR_SUCCESS) {
			return status;
		}
		n->if_next = -1;
	}	

	if ((states & TEMPLATE_SCLOSE) != 0) {
		block = (block_t *) apr_array_pop(p->b);
		if (block == NULL || block->type != TEMPLATE_TIF) {
			return parse_error(p, "no 'if' to close");
		}

		if (block->if_last != -1) {
			n = ((template_node_t *) p->t->elts) + block->if_last;
			n->if_next = p->t->nelts;
		}

		n = ((template_node_t *) p->t->elts) + block->if_start;
		while (block->if_cnt > 0) {
			n = ((template_node_t *) p->t->elts) + n->if_next;
			(n - 1)->jump_next = p->t->nelts;
			block->if_cnt--;
		}
	}
	
	return APR_SUCCESS;
}

/*
 * Processes a 'elseif' element.
 */
static apr_status_t process_elseif (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
	block_t *block;
	template_node_t *n;
	apr_status_t status;

	if ((states & TEMPLATE_SOPEN) != 0) {
		if (apr_is_empty_array(p->b)) {
			return parse_error(p, "no 'if' to continue");
		}
		block = ((block_t *) p->b->elts) + p->b->nelts - 1;
		if (block->type != TEMPLATE_TIF) {
			return parse_error(p, "no 'if' to continue");
		}

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TJUMP;
		n->jump_next = -1;
		block->if_cnt++;

		n = ((template_node_t *) p->t->elts) + block->if_last;
		n->if_next = p->t->nelts;
		block->if_last = p->t->nelts;

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TIF;
		n->if_cond = apr_table_get(attrs, "cond");
		if (n->if_cond == NULL) {
			return parse_error(p, "missing attribute 'cond'");
		}
		if ((status = compile_exp(p, n->if_cond, &n->if_index))
				!= APR_SUCCESS) {
			return status;
		}
		n->if_next = -1;
	}

	return APR_SUCCESS;
}

/*
 * Processes a 'else' element.
 */
static apr_status_t process_else (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
	block_t *block;
	template_node_t *n;

	if ((states & TEMPLATE_SOPEN) != 0) {
		if (apr_is_empty_array(p->b)) {
			return parse_error(p, "no 'if' to continue");
		}
		block = ((block_t *) p->b->elts) + p->b->nelts - 1;
		if (block->type != TEMPLATE_TIF) {
			return parse_error(p, "no 'if' to continue");
		}

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TJUMP;
		n->jump_next = -1;
		block->if_cnt++;

		n = ((template_node_t *) p->t->elts) + block->if_last;
		n->if_next = p->t->nelts;
		block->if_last = -1;
	}

	return APR_SUCCESS;
} 

/*
 * Processes a 'for' element.
 */
static apr_status_t process_for (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
	template_node_t *n;
	block_t *block;
	apr_status_t status;
	const char *names;
	char *name, *last;
	const char *sep = ", \t";

	if ((states & TEMPLATE_SOPEN) != 0) {
		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TFOR_INIT;
		n->for_init_in = apr_table_get(attrs, "in");
		if (n->for_init_in == NULL) {
			return parse_error(p, "missing attribute 'in'");
		}
		if ((status = compile_exp(p, n->for_init_in,
				&n->for_init_index)) != APR_SUCCESS) {
			return status;
		}

		block = (block_t *) apr_array_push(p->b);
		block->type = TEMPLATE_TFOR_NEXT;
		block->for_start = p->t->nelts;

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TFOR_NEXT;
		n->for_next_names = apr_array_make(p->pool, 2,
				sizeof(const char *));
		names = apr_table_get(attrs, "names");
		if (names == NULL) {
			return parse_error(p, "missing attribute 'names'");
		}
		name = apr_strtok(apr_pstrdup(p->pool, names), sep, &last);
		while (name != NULL) {
			*((char **) apr_array_push(n->for_next_names)) = name;	
			name = apr_strtok(NULL, sep, &last);
		}
		if (n->for_next_names->nelts == 0) {
			return parse_error(p, "empty 'names'");
		}
		n->for_next_next = -1;
	}

	if ((states & TEMPLATE_SCLOSE) != 0) {
		block = (block_t *) apr_array_pop(p->b);
                if (block == NULL || block->type != TEMPLATE_TFOR_NEXT) {
                        return parse_error(p, "no 'for' to close");
		}

		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TJUMP;
		n->jump_next = block->for_start;

                n = ((template_node_t *) p->t->elts) + block->for_start;
                n->for_next_next = p->t->nelts;
        }

	return APR_SUCCESS;
}

/*
 * Parses a set element.
 */
static apr_status_t process_set (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
        template_node_t *n;
	const char *names;
        char *name, *last;
        const char *sep = ", \t";
        int status;

        if ((states & TEMPLATE_SOPEN) != 0) {
		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TSET;
                n->set_names = apr_array_make(p->pool, 2, sizeof(const char *));
                names = apr_table_get(attrs, "names");
                if (names == NULL) {
                        return parse_error(p, "missing attribute 'names'");
                }
                name = apr_strtok(apr_pstrdup(p->pool, names), sep, &last);
                while (name != NULL) {
                        *((char **) apr_array_push(n->set_names)) = name;
                        name = apr_strtok(NULL, sep, &last);
                }
                if (apr_is_empty_array(n->set_names)) {
                        return parse_error(p, "empty 'names'");
                }
                n->set_expressions = apr_table_get(attrs, "expressions");
                if (n->set_expressions == NULL) {
                        return parse_error(p, "missing attribute "
					"'expressions'");
                }
                if ((status = compile_exp(p, n->set_expressions, &n->set_index))
				!= APR_SUCCESS) {
                        return status;
                }
	}

	return APR_SUCCESS;
}

/*
 * Processes an 'include' element.
 */
static apr_status_t process_include (parser_rec *p, const char *element,
		int states, apr_table_t *attrs) {
	template_node_t *n;
	int status;

	if ((states & TEMPLATE_SOPEN) != 0) {
		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TINCLUDE;
		n->include_filename = apr_table_get(attrs, "filename");
		if (n->include_filename == NULL) {
			return parse_error(p, "missing attribute 'filename'");
		}
		if ((status = compile_exp(p, n->include_filename,
				&n->include_index)) != APR_SUCCESS) {
			return status;
		}
		n->include_flags = apr_table_get(attrs, "flags");
	}

	return APR_SUCCESS;
}

/*
 * Maps element names to element processors.
 */
static apr_hash_t *element_processors;

/*
 * Adds a single element processor.
 */
static void add_element_processor (const char *element, element_processor ep) {
	apr_hash_set(element_processors, element, strlen(element), ep);
}

/**
 * Initializes the element processor hash.
 */
static void init_element_processors (apr_pool_t *pool) {
	element_processors = apr_hash_make(pool);
	add_element_processor("if", process_if);
	add_element_processor("elseif", process_elseif);
	add_element_processor("else", process_else);
	add_element_processor("for", process_for);
	add_element_processor("set", process_set);
	add_element_processor("include", process_include);
}
	
/*
 * Parses an element.
 */	
static apr_status_t parse_element (parser_rec *p) {
	int states;
	char *element, *key, *val;
	apr_table_t *attrs;
	element_processor ep;
	apr_status_t status;

	p->pos++;
	if (*p->pos == '/') {
		states = TEMPLATE_SCLOSE;
		p->pos++;
	} else {
		states = TEMPLATE_SOPEN;
	}
	p->pos += 2;
		
	p->begin = p->pos;
	while (!isspace(*p->pos) && *p->pos != '>' && *p->pos != '\0') {
		p->pos++;
	}
	element = apr_pstrndup(p->pool, p->begin, p->pos - p->begin);
	while (isspace(*p->pos)) {
		p->pos++;
	}
	attrs = apr_table_make(p->pool, 2);
	while (*p->pos != '>' && *p->pos != '/' && *p->pos != '\0') {
		p->begin = p->pos;
		while (!isspace(*p->pos) && *p->pos != '=' && *p->pos != '\0') {
			p->pos++;
		}
		if (p->pos == p->begin) {
			return parse_error(p, apr_psprintf(p->pool,
					"attribute expected following '%s'",
					element));
		}
		key = apr_pstrndup(p->pool, p->begin, p->pos - p->begin);
		unescape_xml(key);
		while (isspace(*p->pos)) {
			p->pos++;
		}
		if (*p->pos != '=') {
			return parse_error(p, apr_psprintf(p->pool,
					"'=' expected following '%s'", key));
		}
		p->pos++;
		while (isspace(*p->pos)) {
			p->pos++;
		}
		if (*p->pos != '"') {
			return parse_error(p, apr_psprintf(p->pool,
					"'\"' expected following '%s'", key));
		}
		p->pos++;
		p->begin = p->pos;
		while (*p->pos != '"' && *p->pos != '\0') {
			p->pos++;
		}
		if (*p->pos != '"') {
			return parse_error(p, apr_psprintf(p->pool,
					"'\"' expected following '%s'", key));
		}
		val = apr_pstrndup(p->pool, p->begin, p->pos - p->begin);
		unescape_xml(val);
		p->pos++;
		apr_table_setn(attrs, key, val);
		while (isspace(*p->pos)) {
			p->pos++;
		}
	}
	if (*p->pos == '/') {
		states |= TEMPLATE_SCLOSE;
		p->pos++;
	}
	if (*p->pos != '>') {
		return parse_error(p, apr_psprintf(p->pool,
				"'>' expected following '%s'", element));
	}
	p->pos++;

	ep = (element_processor) apr_hash_get(element_processors, element,
			strlen(element));
	if (ep == NULL) {
		return parse_error(p, apr_psprintf(p->pool,
				"unknown element '%s'", element));
	}
	if ((status = ep(p, element, states, attrs)) != APR_SUCCESS) {
		return status;
	}

	return APR_SUCCESS;
}	

/*
 * Parses a substitution.
 */
static apr_status_t parse_sub (parser_rec *p) {
	template_node_t *n;
	int braces, quot;
	apr_status_t status;
	
	n = (template_node_t *) apr_array_push(p->t);
	n->type = TEMPLATE_TSUB;
	p->pos++;

	/* optional flags */
	if (*p->pos == '[') {
		p->pos++;
		p->begin = p->pos;
		while (*p->pos != ']' && *p->pos != '\0') {
			p->pos++;
		}
		if (*p->pos != ']') {
			return parse_error(p, "']' expected");
		}
		n->sub_flags = parse_flags(apr_pstrndup(p->pool, p->begin,
				p->pos - p->begin));
		p->pos++;
	} else {
		n->sub_flags = p->flags;
	}

	/* expression */
	if (*p->pos != '{') {
		return parse_error(p, "'{' expected");
	}
	braces = 1;
	quot = 0;
	p->pos++;
	p->begin = p->pos;
	while (*p->pos != '\0' && braces > 0) {
		switch (*p->pos) {
		case '{':
			if (!quot) {
				braces++;
			}
			break;

		case '}':
			if (!quot) {	
				braces--;
			}
			break;
		
		case '"':
			switch (quot) {
			case 0:
				quot = 1;
				break;

			case 1:
				quot = 0;
				break;
			}
			break;

		case '\'':
			switch (quot) {
			case 0:
				quot = 2;
				break;

			case 2:
				quot = 0;
				break;
			}
			break;

		case '\\':
			switch (quot) {
			case 1:
				if (p->pos[1] == '"') {
					p->pos++;
				}
				break;

			case 2:
				if (p->pos[1] == '\'') {
					p->pos++;
				}
				break;
			}
			break;

		}
		p->pos++;
	}
	if (braces > 0) {
		return parse_error(p, "'}' expected");
	}
	n->sub_exp = apr_pstrndup(p->pool, p->begin, p->pos - p->begin - 1);
	unescape_xml(n->sub_exp);
	if ((status = compile_exp(p, n->sub_exp, &n->sub_index))
			!= APR_SUCCESS) {
		return status;
	}

	return APR_SUCCESS;
}

/*
 * Emits raw template content.
 */
static void parse_raw (parser_rec *p) {
	template_node_t *n;

	if (p->pos != p->begin) {
		n = (template_node_t *) apr_array_push(p->t);
		n->type = TEMPLATE_TRAW;
		n->raw_str = p->begin;
		n->raw_len = p->pos - p->begin;
	}
}

/*
 * Closes a file.
 */
static apr_status_t file_close (void *ud) {
	apr_file_t *file;

	file = (apr_file_t *) ud;
	return apr_file_close(file);
}

/*
 * Prepares a template file.
 */
static apr_status_t parse_template (parser_rec *p) {
	apr_status_t status;
	apr_finfo_t finfo;
	apr_file_t *file;
	apr_size_t size;

	/* read file */
	if ((status = apr_stat(&finfo, p->filename, APR_FINFO_SIZE, p->pool))
			!= APR_SUCCESS) {
		p->err = apr_psprintf(p->pool, "file '%s' does not exist",
				p->filename);
		return status;
	}
	if ((status = apr_file_open(&file, p->filename, APR_READ, 0, p->pool))
			!= APR_SUCCESS) {
		p->err = apr_psprintf(p->pool, "file '%s' cannot be opened",
				p->filename);
		return status;
	}
	apr_pool_cleanup_register(p->pool, file, file_close,
			apr_pool_cleanup_null);
	p->buf = apr_palloc(p->pool, finfo.size + 1);
	size = finfo.size;
	if ((status = apr_file_read(file, p->buf, &size)) != APR_SUCCESS) {
		p->err = apr_psprintf(p->pool, "file '%s' cannot be read",
				p->filename);
		return status;
	}
	if ((status = apr_file_close(file)) != APR_SUCCESS) {
		p->err = apr_psprintf(p->pool, "file '%s' cannot be closed",
				p->filename);
		return status;
	}
	apr_pool_cleanup_kill(p->pool, file, file_close);
	p->buf[size] = '\0';

	/* no parse? */
	if ((p->flags & TEMPLATE_FPARSE) == 0) {
		p->pos = p->buf + size;
		p->begin = p->buf;
		parse_raw(p);
		return APR_SUCCESS;
	}

	/* process elements and substitution, treat all else as raw */
	p->pos = p->buf;
	p->begin = p->pos;
	while (*p->pos != '\0') {
		switch (*p->pos) {
		case '<':
			if ((p->pos[1] == 'l' && p->pos[2] == ':')
					|| (p->pos[1] == '/'
					&& p->pos[2] == 'l'
					&& p->pos[3] == ':')) {
				parse_raw(p);
				if ((status = parse_element(p))
						!= APR_SUCCESS) {
					return status;
				}
				p->begin = p->pos;
			} else {
				p->pos++;
			}
			break;

		case '$':
			switch (p->pos[1]) {
			case '{':
			case '[':
				parse_raw(p);
				if ((status = parse_sub(p)) != APR_SUCCESS) {
					return status;
				}
				p->begin = p->pos;
				break;

			case '$':
				p->pos++;
				parse_raw(p);
				p->pos++;
				p->begin = p->pos;
				break;

			default:
				p->pos++;
			}
			break;

		default:
			p->pos++;
		}
	} 
	parse_raw(p);

	return APR_SUCCESS;
}

/*
 * Renders a template.
 */
static apr_status_t render_template (render_rec *d) {
        int i, cnt;
        template_node_t *n;
        apr_status_t status;
	const char *str;
	apr_array_header_t *t_save;

	d->depth++;
	if (d->depth > TEMPLATE_MAX_DEPTH) {
		d->err = apr_psprintf(d->pool, "template depth exceeds %d",
				TEMPLATE_MAX_DEPTH);
		return APR_EGENERAL;
	}

	i = 0;
	while (i < d->t->nelts) {
		n = ((template_node_t *) d->t->elts) + i;
		switch (n->type) {
		case TEMPLATE_TJUMP:
			i = n->jump_next;
			break;

		case TEMPLATE_TIF:
			if ((status = evaluate_exp(d, n->if_index, 1))
					!= APR_SUCCESS) {
				return status;
			} 
			if (lua_toboolean(d->L, -1)) {
				i++;
			} else {
				i = n->if_next;
			}
			lua_pop(d->L, 1);
			break;

		case TEMPLATE_TFOR_INIT:
			if ((status = evaluate_exp(d, n->for_init_index, 3))
					!= APR_SUCCESS) {
				return status;
			}
			i++;
			break;

		case TEMPLATE_TFOR_NEXT:
			lua_pushvalue(d->L, -3);
			lua_pushvalue(d->L, -3);
			lua_pushvalue(d->L, -3);
			cnt = n->for_next_names->nelts;
			switch (lua_pcall(d->L, 2, cnt, d->errfunc)) {
			case LUA_ERRRUN:
			case LUA_ERRMEM:
			case LUA_ERRERR:
				return runtime_error(d);
			}
			if (lua_isnil(d->L, -cnt)) {
				lua_pop(d->L, 3 + cnt);
				i = n->for_next_next;
			} else {
				lua_pushvalue(d->L, -cnt);
				lua_replace(d->L, -1 - cnt - 1);
				while (cnt > 0) {
					cnt--;
					lua_setglobal(d->L, ((const char **)
							n->for_next_names
							->elts)[cnt]);
				}
				i++;
			}
			break;

		case TEMPLATE_TSET:
			cnt = n->set_names->nelts;
			if ((status = evaluate_exp(d, n->set_index, cnt))
					!= APR_SUCCESS) {
				return status;
			}
			while (cnt > 0) {
				cnt--;
				lua_setglobal(d->L, ((const char **)
						n->set_names->elts)[cnt]);
			}
			i++;
			break;
 
		case TEMPLATE_TINCLUDE:
			if ((status = evaluate_exp_str(d, n->include_index))
					!= APR_SUCCESS) {
				return status;
			}
			str = lua_tostring(d->L, -1);
			lua_pop(d->L, 1);
			t_save = d->t;
			d->t = (apr_array_header_t *) apr_hash_get(d->templates,
					str, strlen(str));
			if (d->t == NULL) {
				if ((status = lwt_template_parse(str, d->L,
						n->include_flags, d->pool,
						&d->t, &d->err))
						!= APR_SUCCESS) {
					return status;
				}
				apr_hash_set(d->templates, str, strlen(str),
						d->t);
			}
			if ((status = render_template(d)) != APR_SUCCESS) {
				return status;
			}
			d->t = t_save;
			i++;
			break;			

		case TEMPLATE_TSUB:
			lua_rawgeti(d->L, LUA_REGISTRYINDEX, n->sub_index);
			switch (lua_pcall(d->L, 0, 1, d->errfunc)) {
			case LUA_ERRRUN:
				if (n->sub_flags & TEMPLATE_FSUPERR) {
					str = "";
				} else {
					return runtime_error(d);
				}
				break;

			case LUA_ERRMEM:
			case LUA_ERRERR:
				return runtime_error(d);

			default:
				if (lua_isstring(d->L, -1)) {
					str = lua_tostring(d->L, -1);
				} else if (lua_isnil(d->L, -1) && (n->sub_flags
						& TEMPLATE_FSUPNIL)) {
					str = "";
				} else {
					str = apr_psprintf(d->pool, "(%s)",
							luaL_typename(d->L,
							-1));
				}
			}
			lua_pop(d->L, 1);
			if (n->sub_flags & TEMPLATE_FESCURL) {
				str = lwt_util_escape_url(d->pool, str);
			}
			if (n->sub_flags & TEMPLATE_FESCXML) {
				str = ap_escape_html(d->pool, str);
			}
			fputs(str, d->f);
			i++;
			break;

		case TEMPLATE_TRAW:
			fwrite(n->raw_str, n->raw_len, 1, d->f);
			i++;
			break;
		}
	}

	d->depth--;

	return APR_SUCCESS;
}

/*
 * Exported functions.
 */

void lwt_template_init (apr_pool_t *pool) {
	init_element_processors(pool);
}

apr_status_t lwt_template_parse (const char *filename, lua_State *L,
		const char *flags, apr_pool_t *pool, apr_array_header_t **t,
		const char **err) {
	parser_rec *p;
	apr_status_t status;

	p = (parser_rec *) apr_pcalloc(pool, sizeof(parser_rec));	
	p->filename = filename;
	p->L = L;
	p->flags = parse_flags(flags != NULL ? flags : TEMPLATE_DEFAULT_FLAGS);
	p->pool = pool;
	p->t = apr_array_make(pool, 32, sizeof(template_node_t));
	p->b = apr_array_make(pool, 8, sizeof(block_t));	
	status = parse_template(p);
	if (status == APR_SUCCESS) {
		if (!apr_is_empty_array(p->b)) {
			status = parse_error(p, apr_psprintf(p->pool,
					"%d open elements at end of template",
					p->b->nelts));
		}
	}
	if (status != APR_SUCCESS) {
		if (err != NULL) {
			*err = p->err;
		}
		return status;
	}

	if (t != NULL) {
		*t = p->t;
	}

	return APR_SUCCESS;
} 

apr_status_t lwt_template_render (apr_array_header_t *t, lua_State *L,
		apr_pool_t *pool, FILE *f, const char **err) {
	render_rec *d;
	apr_status_t status;

	d = (render_rec *) apr_pcalloc(pool, sizeof(render_rec));
	d->t = t;
	d->L = L;
	d->pool = pool;
	d->f = f;
	d->templates = apr_hash_make(pool);

	lua_pushcfunction(d->L, lwt_util_traceback);
	d->errfunc = lua_gettop(d->L);
	if ((status = render_template(d)) != APR_SUCCESS) {
		if (err != NULL) {
			*err = d->err;
		}
		return status;
	}
	lua_pop(d->L, 1);

	return APR_SUCCESS;
}

apr_status_t lwt_template_dump (apr_array_header_t *t, lua_State *L, FILE *f,
		const char **err) {
	int i;
	template_node_t *n;

	fputs("<ol start=\"0\">\r\n", f);
	for (i = 0; i < t->nelts; i++) {
		fputs("<li>", f);
		n = ((template_node_t *) t->elts) + i;
		switch (n->type) {
		case TEMPLATE_TJUMP:
			fprintf(f, "JUMP next=%d", n->jump_next);
			break;

		case TEMPLATE_TIF:
			fprintf(f, "IF cond=%s next=%d", n->if_cond,
				n->if_next);
			break;

		case TEMPLATE_TFOR_INIT:
			fprintf(f, "FOR_INIT in=%s", n->for_init_in);
			break;

		case TEMPLATE_TFOR_NEXT:
			fprintf(f, "FOR_NEXT names=#%d next=%d",
					n->for_next_names->nelts,
					n->for_next_next);
			break;

		case TEMPLATE_TSET:
			fprintf(f, "SET names=#%d expressions=%s",
					n->set_names->nelts,
					n->set_expressions);
			break;

		case TEMPLATE_TINCLUDE:
			fprintf(f, "INCLUDE filename=%s flags=%s",
					n->include_filename, n->include_flags);
			break;

		case TEMPLATE_TSUB:
			fprintf(f, "SUB exp=%s flags=%d", n->sub_exp,
					n->sub_flags);
			break;

		case TEMPLATE_TRAW:
			fprintf(f, "RAW len=%d", n->raw_len);
			break;
		}
		fputs("</li>\r\n", f);
	}
	fputs("</ol>\r\n", f);
		
	return APR_SUCCESS;
}
