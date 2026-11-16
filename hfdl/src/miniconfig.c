/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FoxSDR helper: see miniconfig.h. Written for this helper (not libconfig code). */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "miniconfig.h"

/***************************** tree ******************************/

static config_setting_t *setting_new(int type, char const *name, config_setting_t *parent) {
	config_setting_t *s = calloc(1, sizeof(*s));
	if(s == NULL) {
		return NULL;
	}
	s->type = type;
	s->parent = parent;
	if(name != NULL) {
		s->name = _strdup(name);
	}
	return s;
}

static void setting_free(config_setting_t *s) {
	if(s == NULL) {
		return;
	}
	for(int i = 0; i < s->elem_cnt; i++) {
		setting_free(s->elems[i]);
	}
	free(s->elems);
	free(s->name);
	free(s->sval);
	free(s);
}

static int is_aggregate(int type) {
	return type == CONFIG_TYPE_GROUP || type == CONFIG_TYPE_LIST || type == CONFIG_TYPE_ARRAY;
}

static int setting_append(config_setting_t *parent, config_setting_t *child) {
	if(parent->elem_cnt == parent->elem_cap) {
		int cap = parent->elem_cap ? parent->elem_cap * 2 : 8;
		config_setting_t **n = realloc(parent->elems, (size_t)cap * sizeof(*n));
		if(n == NULL) {
			return 0;
		}
		parent->elems = n;
		parent->elem_cap = cap;
	}
	parent->elems[parent->elem_cnt++] = child;
	return 1;
}

void config_init(config_t *cfg) {
	memset(cfg, 0, sizeof(*cfg));
	cfg->root = setting_new(CONFIG_TYPE_GROUP, NULL, NULL);
}

void config_destroy(config_t *cfg) {
	setting_free(cfg->root);
	cfg->root = NULL;
}

config_setting_t *config_root_setting(config_t const *cfg) {
	return cfg->root;
}

int config_setting_type(config_setting_t const *s) {
	return s ? s->type : CONFIG_TYPE_NONE;
}
int config_setting_is_group(config_setting_t const *s) {
	return s && s->type == CONFIG_TYPE_GROUP;
}
int config_setting_is_list(config_setting_t const *s) {
	return s && s->type == CONFIG_TYPE_LIST;
}
int config_setting_is_number(config_setting_t const *s) {
	return s && (s->type == CONFIG_TYPE_INT || s->type == CONFIG_TYPE_INT64 || s->type == CONFIG_TYPE_FLOAT);
}

config_setting_t *config_setting_get_member(config_setting_t const *s, char const *name) {
	if(s == NULL || s->type != CONFIG_TYPE_GROUP || name == NULL) {
		return NULL;
	}
	for(int i = 0; i < s->elem_cnt; i++) {
		if(s->elems[i]->name != NULL && strcmp(s->elems[i]->name, name) == 0) {
			return s->elems[i];
		}
	}
	return NULL;
}

config_setting_t *config_setting_get_elem(config_setting_t const *s, unsigned int idx) {
	if(s == NULL || !is_aggregate(s->type) || idx >= (unsigned int)s->elem_cnt) {
		return NULL;
	}
	return s->elems[idx];
}

config_setting_t *config_lookup(config_t const *cfg, char const *path) {
	config_setting_t *s = cfg->root;
	char buf[256];
	if(path == NULL || strlen(path) >= sizeof(buf)) {
		return NULL;
	}
	strcpy(buf, path);
	for(char *tok = strtok(buf, "."); tok != NULL && s != NULL; tok = strtok(NULL, ".")) {
		s = config_setting_get_member(s, tok);
	}
	return s;
}

int config_setting_get_int(config_setting_t const *s) {
	if(s == NULL) {
		return 0;
	}
	if(s->type == CONFIG_TYPE_FLOAT) {
		return (int)s->fval;
	}
	return (int)s->ival;
}

double config_setting_get_float(config_setting_t const *s) {
	if(s == NULL) {
		return 0.0;
	}
	if(s->type == CONFIG_TYPE_INT || s->type == CONFIG_TYPE_INT64) {
		return (double)s->ival;
	}
	return s->fval;
}

char const *config_setting_get_string(config_setting_t const *s) {
	return (s != NULL && s->type == CONFIG_TYPE_STRING) ? s->sval : NULL;
}

int config_lookup_int(config_t const *cfg, char const *path, int *value) {
	config_setting_t *s = config_lookup(cfg, path);
	if(s == NULL || (s->type != CONFIG_TYPE_INT && s->type != CONFIG_TYPE_INT64)) {
		return CONFIG_FALSE;
	}
	*value = (int)s->ival;
	return CONFIG_TRUE;
}

int config_setting_lookup_int(config_setting_t const *s, char const *name, int *value) {
	config_setting_t *m = config_setting_get_member(s, name);
	if(m == NULL || (m->type != CONFIG_TYPE_INT && m->type != CONFIG_TYPE_INT64)) {
		return CONFIG_FALSE;
	}
	*value = (int)m->ival;
	return CONFIG_TRUE;
}

int config_setting_lookup_string(config_setting_t const *s, char const *name, char const **value) {
	config_setting_t *m = config_setting_get_member(s, name);
	if(m == NULL || m->type != CONFIG_TYPE_STRING) {
		return CONFIG_FALSE;
	}
	*value = m->sval;
	return CONFIG_TRUE;
}

config_setting_t *config_setting_add(config_setting_t *parent, char const *name, int type) {
	if(parent == NULL || !is_aggregate(parent->type) || type == CONFIG_TYPE_NONE) {
		return NULL;
	}
	if(parent->type == CONFIG_TYPE_GROUP) {
		if(name == NULL || config_setting_get_member(parent, name) != NULL) {
			return NULL;
		}
	} else if(name != NULL) {
		return NULL;
	}
	config_setting_t *s = setting_new(type, name, parent);
	if(s == NULL) {
		return NULL;
	}
	if(!setting_append(parent, s)) {
		setting_free(s);
		return NULL;
	}
	return s;
}

int config_setting_set_int(config_setting_t *s, int value) {
	if(s == NULL || (s->type != CONFIG_TYPE_INT && s->type != CONFIG_TYPE_INT64)) {
		return CONFIG_FALSE;
	}
	s->ival = value;
	return CONFIG_TRUE;
}

int config_setting_set_float(config_setting_t *s, double value) {
	if(s == NULL || s->type != CONFIG_TYPE_FLOAT) {
		return CONFIG_FALSE;
	}
	s->fval = value;
	return CONFIG_TRUE;
}

int config_setting_set_string(config_setting_t *s, char const *value) {
	if(s == NULL || s->type != CONFIG_TYPE_STRING || value == NULL) {
		return CONFIG_FALSE;
	}
	char *copy = _strdup(value);
	if(copy == NULL) {
		return CONFIG_FALSE;
	}
	free(s->sval);
	s->sval = copy;
	return CONFIG_TRUE;
}

config_setting_t *config_setting_set_float_elem(config_setting_t *parent, int idx, double value) {
	if(parent == NULL || (parent->type != CONFIG_TYPE_LIST && parent->type != CONFIG_TYPE_ARRAY)) {
		return NULL;
	}
	if(idx >= 0) {
		config_setting_t *e = config_setting_get_elem(parent, (unsigned int)idx);
		if(e == NULL || e->type != CONFIG_TYPE_FLOAT) {
			return NULL;
		}
		e->fval = value;
		return e;
	}
	config_setting_t *e = config_setting_add(parent, NULL, CONFIG_TYPE_FLOAT);
	if(e != NULL) {
		e->fval = value;
	}
	return e;
}

char const *config_error_text(config_t const *cfg) {
	return cfg->error_text;
}
int config_error_line(config_t const *cfg) {
	return cfg->error_line;
}
config_error_t config_error_type(config_t const *cfg) {
	return cfg->error_type;
}

/***************************** parser ******************************/

struct parser {
	char const *p;
	int line;
	config_t *cfg;
	int failed;
};

static void fail(struct parser *ps, char const *msg) {
	if(!ps->failed) {
		ps->failed = 1;
		ps->cfg->error_type = CONFIG_ERR_PARSE;
		ps->cfg->error_line = ps->line;
		strncpy(ps->cfg->error_text, msg, sizeof(ps->cfg->error_text) - 1);
		ps->cfg->error_text[sizeof(ps->cfg->error_text) - 1] = '\0';
	}
}

static void skip_ws(struct parser *ps) {
	for(;;) {
		char c = *ps->p;
		if(c == '\n') {
			ps->line++;
			ps->p++;
		} else if(c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
			ps->p++;
		} else if(c == '#' || (c == '/' && ps->p[1] == '/')) {
			while(*ps->p != '\0' && *ps->p != '\n') {
				ps->p++;
			}
		} else if(c == '/' && ps->p[1] == '*') {
			ps->p += 2;
			while(*ps->p != '\0' && !(ps->p[0] == '*' && ps->p[1] == '/')) {
				if(*ps->p == '\n') {
					ps->line++;
				}
				ps->p++;
			}
			if(*ps->p == '\0') {
				fail(ps, "unterminated comment");
				return;
			}
			ps->p += 2;
		} else {
			return;
		}
	}
}

static int parse_value(struct parser *ps, config_setting_t *dst);

static int parse_string(struct parser *ps, config_setting_t *dst) {
	char *buf = malloc(strlen(ps->p) + 1);
	size_t n = 0;
	if(buf == NULL) {
		fail(ps, "out of memory");
		return 0;
	}
	ps->p++;    // opening quote
	while(*ps->p != '"') {
		if(*ps->p == '\0' || *ps->p == '\n') {
			free(buf);
			fail(ps, "unterminated string");
			return 0;
		}
		if(*ps->p == '\\') {
			ps->p++;
			switch(*ps->p) {
			case 'n': buf[n++] = '\n'; break;
			case 't': buf[n++] = '\t'; break;
			case 'r': buf[n++] = '\r'; break;
			case 'f': buf[n++] = '\f'; break;
			case '\\': buf[n++] = '\\'; break;
			case '"': buf[n++] = '"'; break;
			default: free(buf); fail(ps, "bad escape in string"); return 0;
			}
			ps->p++;
		} else {
			buf[n++] = *ps->p++;
		}
	}
	ps->p++;    // closing quote
	buf[n] = '\0';
	dst->type = CONFIG_TYPE_STRING;
	dst->sval = buf;
	return 1;
}

static int parse_number(struct parser *ps, config_setting_t *dst) {
	char const *start = ps->p;
	char *end = NULL;
	int is_float = 0;
	char const *q = start;
	if(*q == '+' || *q == '-') {
		q++;
	}
	if(q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
		errno = 0;
		long long v = strtoll(start, &end, 16);
		if(end == start) {
			fail(ps, "bad number");
			return 0;
		}
		ps->p = end;
		dst->type = CONFIG_TYPE_INT;
		dst->ival = v;
	} else {
		for(char const *c = q; isdigit((unsigned char)*c) || *c == '.' || *c == 'e' || *c == 'E' ||
				((*c == '+' || *c == '-') && (c[-1] == 'e' || c[-1] == 'E')); c++) {
			if(*c == '.' || *c == 'e' || *c == 'E') {
				is_float = 1;
			}
		}
		if(is_float) {
			double v = strtod(start, &end);
			if(end == start) {
				fail(ps, "bad number");
				return 0;
			}
			ps->p = end;
			dst->type = CONFIG_TYPE_FLOAT;
			dst->fval = v;
		} else {
			long long v = strtoll(start, &end, 10);
			if(end == start) {
				fail(ps, "bad number");
				return 0;
			}
			ps->p = end;
			dst->type = CONFIG_TYPE_INT;
			dst->ival = v;
		}
	}
	if(*ps->p == 'L') {      // 64-bit integer suffix
		ps->p++;
		if(dst->type == CONFIG_TYPE_INT) {
			dst->type = CONFIG_TYPE_INT64;
		}
	}
	return 1;
}

static int is_name_start(char c) {
	return isalpha((unsigned char)c) || c == '*';
}
static int is_name_char(char c) {
	return isalnum((unsigned char)c) || c == '*' || c == '_' || c == '-';
}

static int parse_group_body(struct parser *ps, config_setting_t *group, char terminator) {
	for(;;) {
		skip_ws(ps);
		if(ps->failed) {
			return 0;
		}
		if(*ps->p == terminator) {
			return terminator != '\0' ? (ps->p++, 1) : 1;
		}
		if(*ps->p == '\0') {
			fail(ps, "unexpected end of file");
			return 0;
		}
		if(!is_name_start(*ps->p)) {
			fail(ps, "syntax error");
			return 0;
		}
		char const *ns = ps->p;
		while(is_name_char(*ps->p)) {
			ps->p++;
		}
		char name[128];
		size_t nl = (size_t)(ps->p - ns);
		if(nl >= sizeof(name)) {
			fail(ps, "setting name too long");
			return 0;
		}
		memcpy(name, ns, nl);
		name[nl] = '\0';
		skip_ws(ps);
		if(*ps->p != '=' && *ps->p != ':') {
			fail(ps, "syntax error");
			return 0;
		}
		ps->p++;
		config_setting_t *s = setting_new(CONFIG_TYPE_NONE, name, group);
		if(s == NULL || !setting_append(group, s)) {
			fail(ps, "out of memory");
			return 0;
		}
		for(int i = 0; i < group->elem_cnt - 1; i++) {
			if(group->elems[i]->name != NULL && strcmp(group->elems[i]->name, name) == 0) {
				fail(ps, "duplicate setting name");
				return 0;
			}
		}
		if(!parse_value(ps, s)) {
			return 0;
		}
		skip_ws(ps);
		if(*ps->p == ';' || *ps->p == ',') {
			ps->p++;
		}
	}
}

static int parse_seq_body(struct parser *ps, config_setting_t *seq, char terminator) {
	for(;;) {
		skip_ws(ps);
		if(ps->failed) {
			return 0;
		}
		if(*ps->p == terminator) {
			ps->p++;
			return 1;
		}
		if(*ps->p == '\0') {
			fail(ps, "unexpected end of file");
			return 0;
		}
		config_setting_t *e = setting_new(CONFIG_TYPE_NONE, NULL, seq);
		if(e == NULL || !setting_append(seq, e)) {
			fail(ps, "out of memory");
			return 0;
		}
		if(!parse_value(ps, e)) {
			return 0;
		}
		skip_ws(ps);
		if(*ps->p == ',') {
			ps->p++;
		} else if(*ps->p != terminator) {
			fail(ps, "syntax error");
			return 0;
		}
	}
}

static int parse_value(struct parser *ps, config_setting_t *dst) {
	skip_ws(ps);
	char c = *ps->p;
	if(c == '"') {
		return parse_string(ps, dst);
	} else if(c == '{') {
		ps->p++;
		dst->type = CONFIG_TYPE_GROUP;
		return parse_group_body(ps, dst, '}');
	} else if(c == '(') {
		ps->p++;
		dst->type = CONFIG_TYPE_LIST;
		return parse_seq_body(ps, dst, ')');
	} else if(c == '[') {
		ps->p++;
		dst->type = CONFIG_TYPE_ARRAY;
		return parse_seq_body(ps, dst, ']');
	} else if(isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.') {
		return parse_number(ps, dst);
	} else if(strncmp(ps->p, "true", 4) == 0 && !is_name_char(ps->p[4])) {
		ps->p += 4;
		dst->type = CONFIG_TYPE_BOOL;
		dst->ival = 1;
		return 1;
	} else if(strncmp(ps->p, "false", 5) == 0 && !is_name_char(ps->p[5])) {
		ps->p += 5;
		dst->type = CONFIG_TYPE_BOOL;
		dst->ival = 0;
		return 1;
	}
	fail(ps, "syntax error");
	return 0;
}

int config_read_file(config_t *cfg, char const *filename) {
	FILE *f = fopen(filename, "rb");
	cfg->error_type = CONFIG_ERR_NONE;
	cfg->error_line = 0;
	cfg->error_text[0] = '\0';
	if(f == NULL) {
		cfg->error_type = CONFIG_ERR_FILE_IO;
		strncpy(cfg->error_text, "file I/O error", sizeof(cfg->error_text) - 1);
		return CONFIG_FALSE;
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(len < 0 || len > 16 * 1024 * 1024) {
		fclose(f);
		cfg->error_type = CONFIG_ERR_FILE_IO;
		strncpy(cfg->error_text, "file I/O error", sizeof(cfg->error_text) - 1);
		return CONFIG_FALSE;
	}
	char *text = malloc((size_t)len + 1);
	if(text == NULL || fread(text, 1, (size_t)len, f) != (size_t)len) {
		free(text);
		fclose(f);
		cfg->error_type = CONFIG_ERR_FILE_IO;
		strncpy(cfg->error_text, "file I/O error", sizeof(cfg->error_text) - 1);
		return CONFIG_FALSE;
	}
	fclose(f);
	text[len] = '\0';
	setting_free(cfg->root);
	cfg->root = setting_new(CONFIG_TYPE_GROUP, NULL, NULL);
	struct parser ps = { .p = text, .line = 1, .cfg = cfg, .failed = 0 };
	int ok = parse_group_body(&ps, cfg->root, '\0');
	free(text);
	return ok && !ps.failed ? CONFIG_TRUE : CONFIG_FALSE;
}

/***************************** writer ******************************/

static void write_value(FILE *f, config_setting_t const *s, int indent);

static void write_indent(FILE *f, int n) {
	for(int i = 0; i < n; i++) {
		fputs("  ", f);
	}
}

static void write_string(FILE *f, char const *str) {
	fputc('"', f);
	for(; *str != '\0'; str++) {
		switch(*str) {
		case '\n': fputs("\\n", f); break;
		case '\t': fputs("\\t", f); break;
		case '\r': fputs("\\r", f); break;
		case '\f': fputs("\\f", f); break;
		case '\\': fputs("\\\\", f); break;
		case '"': fputs("\\\"", f); break;
		default: fputc(*str, f);
		}
	}
	fputc('"', f);
}

static void write_value(FILE *f, config_setting_t const *s, int indent) {
	switch(s->type) {
	case CONFIG_TYPE_INT:
		fprintf(f, "%lld", s->ival);
		break;
	case CONFIG_TYPE_INT64:
		fprintf(f, "%lldL", s->ival);
		break;
	case CONFIG_TYPE_BOOL:
		fputs(s->ival ? "true" : "false", f);
		break;
	case CONFIG_TYPE_FLOAT: {
		char buf[64];
		snprintf(buf, sizeof(buf), "%.10g", s->fval);
		fputs(buf, f);
		if(strpbrk(buf, ".eE") == NULL) {
			fputs(".0", f);
		}
		break;
	}
	case CONFIG_TYPE_STRING:
		write_string(f, s->sval != NULL ? s->sval : "");
		break;
	case CONFIG_TYPE_GROUP:
		fputs("{\n", f);
		for(int i = 0; i < s->elem_cnt; i++) {
			write_indent(f, indent + 1);
			if(s->elems[i]->name != NULL) {
				fprintf(f, "%s = ", s->elems[i]->name);
			}
			write_value(f, s->elems[i], indent + 1);
			fputs(";\n", f);
		}
		write_indent(f, indent);
		fputc('}', f);
		break;
	case CONFIG_TYPE_LIST:
	case CONFIG_TYPE_ARRAY:
		fputs(s->type == CONFIG_TYPE_LIST ? "( " : "[ ", f);
		for(int i = 0; i < s->elem_cnt; i++) {
			if(i > 0) {
				fputs(", ", f);
			}
			write_value(f, s->elems[i], indent + 1);
		}
		fputs(s->type == CONFIG_TYPE_LIST ? " )" : " ]", f);
		break;
	default:
		break;
	}
}

int config_write_file(config_t *cfg, char const *filename) {
	FILE *f = fopen(filename, "wb");
	if(f == NULL) {
		cfg->error_type = CONFIG_ERR_FILE_IO;
		strncpy(cfg->error_text, "file I/O error", sizeof(cfg->error_text) - 1);
		return CONFIG_FALSE;
	}
	for(int i = 0; i < cfg->root->elem_cnt; i++) {
		config_setting_t const *s = cfg->root->elems[i];
		fprintf(f, "%s = ", s->name != NULL ? s->name : "");
		write_value(f, s, 0);
		fputs(";\n", f);
	}
	return fclose(f) == 0 ? CONFIG_TRUE : CONFIG_FALSE;
}
