#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

// Token types
typedef enum {
    TOK_EOF,
    TOK_DEFN,
    TOK_RETURN,
    TOK_DISPLAY,
    TOK_IDENT,
    TOK_STRING,
    TOK_CHAR,
    TOK_NUMBER,
    TOK_FLOAT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_COLON_COLON,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_TYPE,
    TOK_DOT,
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int col;
} Token;

// Cursor for source tracking
typedef struct {
    const char *source;
    const char *current;
    const char *filename;
    int line;
    int col;
    int scope_level;
} Cursor;

// AST Node types
typedef enum {
    AST_FUNCTION,
    AST_CALL,
    AST_RETURN,
    AST_DISPLAY,
    AST_BLOCK,
} ASTNodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    char *name;
    char *return_type;
    char *docstring;
    ASTNode **body;
    size_t body_count;
    size_t body_capacity;
} FunctionNode;

typedef struct {
    char *name;
} CallNode;

typedef struct {
    char *value;
    char *type;
} ReturnNode;

typedef struct {
    char *message;
    char *type; // "string", "char", or "call"
    char *call_name; // for function calls
} DisplayNode;

typedef struct {
    ASTNode **statements;
    size_t stmt_count;
    size_t stmt_capacity;
} BlockNode;

struct ASTNode {
    ASTNodeType type;
    int line;
    int col;
    union {
        FunctionNode function;
        CallNode call;
        ReturnNode ret;
        DisplayNode display;
        BlockNode block;
    };
};

// Compilation options
typedef struct {
    const char *input_file;
    char *output_file;
    bool emit_ir;
    bool emit_bc;
} CompileOptions;

// Type info structure
typedef struct {
    const char *name;
    bool is_signed;
    bool is_float;
    bool is_bool;
    long long min_int;
    unsigned long long max_uint;
    long long max_int;
    double min_float;
    double max_float;
} TypeInfo;

// Get type information
static TypeInfo get_type_info(const char *type) {
    TypeInfo info = {.name = type, .is_bool = false, .is_signed = false, .is_float = false};
    
    if (strcmp(type, "bool") == 0) {
        info.is_bool = true;
        info.min_int = 0;
        info.max_int = 1;
        return info;
    }
    
    if (strcmp(type, "i8") == 0) {
        info.is_signed = true;
        info.min_int = -128;
        info.max_int = 127;
    } else if (strcmp(type, "u8") == 0) {
        info.is_signed = false;
        info.min_int = 0;
        info.max_uint = 255;
    } else if (strcmp(type, "i16") == 0) {
        info.is_signed = true;
        info.min_int = -32768;
        info.max_int = 32767;
    } else if (strcmp(type, "u16") == 0) {
        info.is_signed = false;
        info.min_int = 0;
        info.max_uint = 65535;
    } else if (strcmp(type, "i32") == 0) {
        info.is_signed = true;
        info.min_int = INT_MIN;
        info.max_int = INT_MAX;
    } else if (strcmp(type, "u32") == 0) {
        info.is_signed = false;
        info.min_int = 0;
        info.max_uint = UINT_MAX;
    } else if (strcmp(type, "i64") == 0) {
        info.is_signed = true;
        info.min_int = LLONG_MIN;
        info.max_int = LLONG_MAX;
    } else if (strcmp(type, "u64") == 0) {
        info.is_signed = false;
        info.min_int = 0;
        info.max_uint = ULLONG_MAX;
    } else if (strcmp(type, "i128") == 0) {
        info.is_signed = true;
        info.min_int = LLONG_MIN;
        info.max_int = LLONG_MAX;
    } else if (strcmp(type, "u128") == 0) {
        info.is_signed = false;
        info.min_int = 0;
        info.max_uint = ULLONG_MAX;
    } else if (strcmp(type, "f32") == 0) {
        info.is_float = true;
        info.min_float = -3.4028235e38;
        info.max_float = 3.4028235e38;
    } else if (strcmp(type, "f64") == 0) {
        info.is_float = true;
        info.min_float = -1.7976931348623157e308;
        info.max_float = 1.7976931348623157e308;
    }
    
    return info;
}

// Type checking
static bool is_valid_type(const char *type) {
    const char *valid_types[] = {
        "void", "bool", "char", "str",
        "i8", "i16", "i32", "i64", "i128",
        "u8", "u16", "u32", "u64", "u128",
        "f32", "f64",
        NULL
    };
    
    for (int i = 0; valid_types[i]; i++) {
        if (strcmp(type, valid_types[i]) == 0) return true;
    }
    return false;
}

// Convert type string to LLVM type
static LLVMTypeRef get_llvm_type(const char *type) {
    if (strcmp(type, "void") == 0) return LLVMVoidType();
    if (strcmp(type, "bool") == 0) return LLVMInt1Type();
    if (strcmp(type, "char") == 0) return LLVMInt8Type();
    if (strcmp(type, "str") == 0) return LLVMPointerType(LLVMInt8Type(), 0);
    if (strcmp(type, "i8") == 0 || strcmp(type, "u8") == 0) return LLVMInt8Type();
    if (strcmp(type, "i16") == 0 || strcmp(type, "u16") == 0) return LLVMInt16Type();
    if (strcmp(type, "i32") == 0 || strcmp(type, "u32") == 0) return LLVMInt32Type();
    if (strcmp(type, "i64") == 0 || strcmp(type, "u64") == 0) return LLVMInt64Type();
    if (strcmp(type, "i128") == 0 || strcmp(type, "u128") == 0) return LLVMIntType(128);
    if (strcmp(type, "f32") == 0) return LLVMFloatType();
    if (strcmp(type, "f64") == 0) return LLVMDoubleType();
    
    return NULL;
}

// Initialize cursor
static inline Cursor cursor_init(const char *source, const char *filename) {
    return (Cursor){
        .source = source,
        .current = source,
        .filename = filename,
        .line = 1,
        .col = 1,
        .scope_level = 0
    };
}

// Advance cursor
static inline char cursor_advance(Cursor *c) {
    if (*c->current == '\0') return '\0';
    char ch = *c->current++;
    if (ch == '\n') {
        c->line++;
        c->col = 1;
    } else {
        c->col++;
    }
    return ch;
}

// Peek current character
static inline char cursor_peek(const Cursor *c) {
    return *c->current;
}

// Skip whitespace and comments
static void cursor_skip_whitespace(Cursor *c) {
    while (true) {
        char ch = cursor_peek(c);
        if (ch == ';') {
            // Skip until end of line
            while (cursor_peek(c) != '\n' && cursor_peek(c) != '\0') {
                cursor_advance(c);
            }
        } else if (isspace(ch)) {
            cursor_advance(c);
        } else {
            break;
        }
    }
}

// Error reporting (C-style for Emacs compatibility)
__attribute__((noreturn))
static void error_at(const Cursor *c, const char *msg) {
    fprintf(stderr, "%s:%d:%d: error: %s\n", c->filename, c->line, c->col, msg);
    exit(1);
}

__attribute__((noreturn))
static void error_at_node(const Cursor *c, const ASTNode *node, const char *msg) {
    fprintf(stderr, "%s:%d:%d: error: %s\n", c->filename, node->line, node->col, msg);
    exit(1);
}

// Check if character is valid in identifier
static bool is_ident_char(char c) {
    return isalnum(c) || c == '_' || c == '-' || c == '>' || c == '?';
}

// Lexer
static Token *lex_token(Cursor *c) {
    cursor_skip_whitespace(c);
    
    Token *tok = malloc(sizeof(Token));
    if (!tok) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    
    tok->line = c->line;
    tok->col = c->col;
    tok->value = NULL;
    
    char ch = cursor_peek(c);
    
    if (ch == '\0') {
        tok->type = TOK_EOF;
        return tok;
    }
    
    if (ch == '{') {
        cursor_advance(c);
        c->scope_level++;
        tok->type = TOK_LBRACE;
        return tok;
    }
    
    if (ch == '}') {
        cursor_advance(c);
        c->scope_level--;
        tok->type = TOK_RBRACE;
        return tok;
    }
    
    if (ch == '.') {
        cursor_advance(c);
        tok->type = TOK_DOT;
        return tok;
    }
    
    if (ch == ':' && c->current[1] == ':') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_COLON_COLON;
        return tok;
    }
    
    if (ch == '"') {
        cursor_advance(c);
        const char *str_start = c->current;
        while (cursor_peek(c) != '"' && cursor_peek(c) != '\0') {
            if (cursor_peek(c) == '\\') {
                cursor_advance(c);
                if (cursor_peek(c) != '\0') cursor_advance(c);
            } else {
                cursor_advance(c);
            }
        }
        if (cursor_peek(c) != '"') {
            error_at(c, "unterminated string");
        }
        size_t len = c->current - str_start;
        tok->value = strndup(str_start, len);
        cursor_advance(c); // skip closing "
        tok->type = TOK_STRING;
        return tok;
    }
    
    if (ch == '\'') {
        cursor_advance(c);
        const char *char_start = c->current;
        if (cursor_peek(c) == '\\') {
            cursor_advance(c);
            if (cursor_peek(c) != '\0') cursor_advance(c);
        } else if (cursor_peek(c) != '\0') {
            cursor_advance(c);
        }
        if (cursor_peek(c) != '\'') {
            error_at(c, "unterminated character literal or multi-character constant");
        }
        size_t len = c->current - char_start;
        tok->value = strndup(char_start, len);
        cursor_advance(c); // skip closing '
        tok->type = TOK_CHAR;
        return tok;
    }
    
    if (isdigit(ch) || (ch == '-' && isdigit(c->current[1]))) {
        const char *num_start = c->current;
        if (ch == '-') cursor_advance(c);
        
        while (isdigit(cursor_peek(c))) {
            cursor_advance(c);
        }
        
        if (cursor_peek(c) == '.') {
            cursor_advance(c);
            while (isdigit(cursor_peek(c))) {
                cursor_advance(c);
            }
            size_t len = c->current - num_start;
            tok->value = strndup(num_start, len);
            tok->type = TOK_FLOAT;
            return tok;
        }
        
        size_t len = c->current - num_start;
        tok->value = strndup(num_start, len);
        tok->type = TOK_NUMBER;
        return tok;
    }
    
    if (isalpha(ch) || ch == '_') {
        const char *ident_start = c->current;
        while (is_ident_char(cursor_peek(c))) {
            cursor_advance(c);
        }
        size_t len = c->current - ident_start;
        tok->value = strndup(ident_start, len);
        
        if (strcmp(tok->value, "defn") == 0) tok->type = TOK_DEFN;
        else if (strcmp(tok->value, "return") == 0) tok->type = TOK_RETURN;
        else if (strcmp(tok->value, "display") == 0) tok->type = TOK_DISPLAY;
        else if (strcmp(tok->value, "true") == 0) tok->type = TOK_TRUE;
        else if (strcmp(tok->value, "false") == 0) tok->type = TOK_FALSE;
        else if (is_valid_type(tok->value)) tok->type = TOK_TYPE;
        else tok->type = TOK_IDENT;
        
        return tok;
    }
    
    error_at(c, "unexpected character");
}

// Parser
typedef struct {
    Cursor *cursor;
    Token *current;
} Parser;

static void parser_advance(Parser *p) {
    free(p->current->value);
    free(p->current);
    p->current = lex_token(p->cursor);
}

static void parser_expect(Parser *p, TokenType type, const char *msg) {
    if (p->current->type != type) {
        error_at(p->cursor, msg);
    }
    parser_advance(p);
}

static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);

static ASTNode *parse_function(Parser *p) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    
    node->type = AST_FUNCTION;
    node->line = p->current->line;
    node->col = p->current->col;
    
    parser_advance(p); // skip 'defn'
    
    if (p->current->type != TOK_IDENT) {
        error_at(p->cursor, "expected function name");
    }
    node->function.name = strdup(p->current->value);
    parser_advance(p);
    
    parser_expect(p, TOK_COLON_COLON, "expected '::'");
    
    if (p->current->type != TOK_TYPE) {
        error_at(p->cursor, "expected return type");
    }
    node->function.return_type = strdup(p->current->value);
    parser_advance(p);
    
    parser_expect(p, TOK_LBRACE, "expected '{'");
    
    // Check for docstring
    node->function.docstring = NULL;
    if (p->current->type == TOK_STRING) {
        node->function.docstring = strdup(p->current->value);
        parser_advance(p);
    }
    
    node->function.body_capacity = 16;
    node->function.body = malloc(sizeof(ASTNode*) * node->function.body_capacity);
    node->function.body_count = 0;
    
    while (p->current->type != TOK_RBRACE && p->current->type != TOK_EOF) {
        if (node->function.body_count >= node->function.body_capacity) {
            node->function.body_capacity *= 2;
            node->function.body = realloc(node->function.body, 
                sizeof(ASTNode*) * node->function.body_capacity);
        }
        node->function.body[node->function.body_count++] = parse_statement(p);
    }
    
    parser_expect(p, TOK_RBRACE, "expected '}'");
    
    return node;
}

static ASTNode *parse_block(Parser *p) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_BLOCK;
    node->line = p->current->line;
    node->col = p->current->col;
    
    parser_expect(p, TOK_LBRACE, "expected '{'");
    
    node->block.stmt_capacity = 16;
    node->block.statements = malloc(sizeof(ASTNode*) * node->block.stmt_capacity);
    node->block.stmt_count = 0;
    
    while (p->current->type != TOK_RBRACE && p->current->type != TOK_EOF) {
        if (node->block.stmt_count >= node->block.stmt_capacity) {
            node->block.stmt_capacity *= 2;
            node->block.statements = realloc(node->block.statements,
                sizeof(ASTNode*) * node->block.stmt_capacity);
        }
        node->block.statements[node->block.stmt_count++] = parse_statement(p);
    }
    
    parser_expect(p, TOK_RBRACE, "expected '}'");
    
    return node;
}

static ASTNode *parse_statement(Parser *p) {
    if (p->current->type == TOK_LBRACE) {
        return parse_block(p);
    }
    
    if (p->current->type == TOK_RETURN) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_RETURN;
        node->line = p->current->line;
        node->col = p->current->col;
        parser_advance(p);
        
        // Handle type.min and type.max
        if (p->current->type == TOK_TYPE) {
            char *type_name = strdup(p->current->value);
            parser_advance(p);
            
            if (p->current->type == TOK_DOT) {
                parser_advance(p);
                if (p->current->type != TOK_IDENT) {
                    error_at(p->cursor, "expected 'min' or 'max' after type name");
                }
                
                TypeInfo info = get_type_info(type_name);
                
                if (strcmp(p->current->value, "min") == 0) {
                    if (info.is_bool) {
                        node->ret.value = strdup("0");
                        node->ret.type = "bool";
                    } else if (info.is_float) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.17g", info.min_float);
                        node->ret.value = strdup(buf);
                        node->ret.type = "float";
                    } else if (strcmp(type_name, "i128") == 0) {
                        node->ret.value = strdup("-170141183460469231731687303715884105728");
                        node->ret.type = "int";
                    } else {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%lld", info.min_int);
                        node->ret.value = strdup(buf);
                        node->ret.type = "int";
                    }
                } else if (strcmp(p->current->value, "max") == 0) {
                    if (info.is_bool) {
                        node->ret.value = strdup("1");
                        node->ret.type = "bool";
                    } else if (info.is_float) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.17g", info.max_float);
                        node->ret.value = strdup(buf);
                        node->ret.type = "float";
                    } else if (strcmp(type_name, "i128") == 0) {
                        node->ret.value = strdup("170141183460469231731687303715884105727");
                        node->ret.type = "int";
                    } else if (strcmp(type_name, "u128") == 0) {
                        node->ret.value = strdup("340282366920938463463374607431768211455");
                        node->ret.type = "int";
                    } else if (info.is_signed) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%lld", info.max_int);
                        node->ret.value = strdup(buf);
                        node->ret.type = "int";
                    } else {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%llu", info.max_uint);
                        node->ret.value = strdup(buf);
                        node->ret.type = "int";
                    }
                } else {
                    error_at(p->cursor, "expected 'min' or 'max' after type name");
                }
                
                parser_advance(p);
                free(type_name);
                return node;
            }
            
            free(type_name);
            error_at(p->cursor, "expected '.' after type name in return statement");
        }
        
        if (p->current->type == TOK_NUMBER) {
            node->ret.value = strdup(p->current->value);
            node->ret.type = "int";
            parser_advance(p);
        } else if (p->current->type == TOK_FLOAT) {
            node->ret.value = strdup(p->current->value);
            node->ret.type = "float";
            parser_advance(p);
        } else if (p->current->type == TOK_STRING) {
            node->ret.value = strdup(p->current->value);
            node->ret.type = "str";
            parser_advance(p);
        } else if (p->current->type == TOK_CHAR) {
            node->ret.value = strdup(p->current->value);
            node->ret.type = "char";
            parser_advance(p);
        } else if (p->current->type == TOK_TRUE) {
            node->ret.value = strdup("1");
            node->ret.type = "bool";
            parser_advance(p);
        } else if (p->current->type == TOK_FALSE) {
            node->ret.value = strdup("0");
            node->ret.type = "bool";
            parser_advance(p);
        } else {
            error_at(p->cursor, "expected value after return");
        }
        return node;
    }
    
    if (p->current->type == TOK_DISPLAY) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_DISPLAY;
        node->line = p->current->line;
        node->col = p->current->col;
        parser_advance(p);
        
        if (p->current->type == TOK_STRING) {
            node->display.message = strdup(p->current->value);
            node->display.type = "string";
            node->display.call_name = NULL;
            parser_advance(p);
        } else if (p->current->type == TOK_CHAR) {
            node->display.message = strdup(p->current->value);
            node->display.type = "char";
            node->display.call_name = NULL;
            parser_advance(p);
        } else if (p->current->type == TOK_IDENT) {
            node->display.message = NULL;
            node->display.type = "call";
            node->display.call_name = strdup(p->current->value);
            parser_advance(p);
        } else {
            error_at(p->cursor, "expected string, character, or function call after display");
        }
        return node;
    }
    
    if (p->current->type == TOK_IDENT) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_CALL;
        node->line = p->current->line;
        node->col = p->current->col;
        node->call.name = strdup(p->current->value);
        parser_advance(p);
        return node;
    }
    
    error_at(p->cursor, "expected statement");
}

static ASTNode **parse(const char *source, const char *filename, size_t *count) {
    Cursor cursor = cursor_init(source, filename);
    Parser parser = {
        .cursor = &cursor,
        .current = lex_token(&cursor)
    };
    
    size_t capacity = 16;
    ASTNode **nodes = malloc(sizeof(ASTNode*) * capacity);
    *count = 0;
    
    while (parser.current->type != TOK_EOF) {
        if (parser.current->type == TOK_DEFN) {
            if (*count >= capacity) {
                capacity *= 2;
                nodes = realloc(nodes, sizeof(ASTNode*) * capacity);
            }
            nodes[(*count)++] = parse_function(&parser);
        } else {
            error_at(&cursor, "expected function definition");
        }
    }
    
    free(parser.current->value);
    free(parser.current);
    return nodes;
}

// Type checking function
static void check_return_type(const Cursor *cursor, const ASTNode *func, const ASTNode *ret_stmt) {
    const char *func_type = func->function.return_type;
    const char *ret_type = ret_stmt->ret.type;
    const char *ret_value = ret_stmt->ret.value;
    
    TypeInfo func_info = get_type_info(func_type);
    
    // Check for char type mismatches
    if (strcmp(ret_type, "char") == 0 && strcmp(func_type, "char") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                "incompatible types when returning type 'char' but '%s' was expected",
                func_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    if (strcmp(func_type, "char") == 0 && strcmp(ret_type, "char") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "incompatible types when returning type '%s' but 'char' was expected",
                ret_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    // Check for str type mismatches
    if (strcmp(ret_type, "str") == 0 && strcmp(func_type, "str") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                "incompatible types when returning type 'str' but '%s' was expected",
                func_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    if (strcmp(func_type, "str") == 0 && strcmp(ret_type, "str") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "incompatible types when returning type '%s' but 'str' was expected",
                ret_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    // Check for type mismatches between bool and other types
    if (strcmp(ret_type, "bool") == 0 && !func_info.is_bool) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                "incompatible types when returning type 'bool' but '%s' was expected",
                func_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    if (func_info.is_bool && strcmp(ret_type, "bool") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "incompatible types when returning type '%s' but 'bool' was expected",
                ret_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    // Check if trying to return float to int type or vice versa
    if (strcmp(ret_type, "float") == 0 && !func_info.is_float && !func_info.is_bool) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
                "incompatible types when returning type 'float' but '%s' was expected",
                func_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    if (strcmp(ret_type, "int") == 0 && func_info.is_float) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                "incompatible types when returning type 'int' but '%s' was expected",
                func_type);
        error_at_node(cursor, ret_stmt, msg);
    }
    
    // Check bool returns
    if (func_info.is_bool && strcmp(ret_type, "int") == 0) {
        long long val = atoll(ret_value);
        if (val != 0 && val != 1) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "value '%s' is out of range for type 'bool' (expected 0 or 1)",
                    ret_value);
            error_at_node(cursor, ret_stmt, msg);
        }
        return;
    }
    
    // Check integer ranges
    if (strcmp(ret_type, "int") == 0 && !func_info.is_float && !func_info.is_bool) {
        if (strcmp(func_type, "i128") == 0 || strcmp(func_type, "u128") == 0) {
            return;
        }
        
        long long val = atoll(ret_value);
        
        if (func_info.is_signed) {
            if (val < func_info.min_int || val > func_info.max_int) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                        "value '%s' is out of range for type '%s' (expected %lld to %lld)",
                        ret_value, func_type, func_info.min_int, func_info.max_int);
                error_at_node(cursor, ret_stmt, msg);
            }
        } else {
            if (val < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                        "value '%s' is out of range for type '%s' (expected 0 to %llu)",
                        ret_value, func_type, func_info.max_uint);
                error_at_node(cursor, ret_stmt, msg);
            }
            unsigned long long uval = (unsigned long long)val;
            if (uval > func_info.max_uint) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                        "value '%s' is out of range for type '%s' (expected 0 to %llu)",
                        ret_value, func_type, func_info.max_uint);
                error_at_node(cursor, ret_stmt, msg);
            }
        }
    }
    
    // Check float ranges
    if (strcmp(ret_type, "float") == 0 && func_info.is_float) {
        double val = atof(ret_value);
        if (val < func_info.min_float || val > func_info.max_float) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "value '%s' is out of range for type '%s'",
                    ret_value, func_type);
            error_at_node(cursor, ret_stmt, msg);
        }
    }
}

// Recursive type checker for statements
static void check_statement_types(const Cursor *cursor, const ASTNode *func, const ASTNode *stmt) {
    if (stmt->type == AST_RETURN) {
        check_return_type(cursor, func, stmt);
    } else if (stmt->type == AST_BLOCK) {
        for (size_t i = 0; i < stmt->block.stmt_count; i++) {
            check_statement_types(cursor, func, stmt->block.statements[i]);
        }
    }
}

// Type check all functions
static void type_check(const Cursor *cursor, ASTNode **nodes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (nodes[i]->type == AST_FUNCTION) {
            for (size_t j = 0; j < nodes[i]->function.body_count; j++) {
                check_statement_types(cursor, nodes[i], nodes[i]->function.body[j]);
            }
        }
    }
}

// Process escape sequences in strings and chars
static char process_single_escape(char c) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '0': return '\0';
        default: return c;
    }
}

static char *process_escapes(const char *str) {
    size_t len = strlen(str);
    char *result = malloc(len + 1);
    size_t j = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            result[j++] = process_single_escape(str[i + 1]);
            i++;
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

static char process_char_literal(const char *str) {
    if (str[0] == '\\' && str[1] != '\0') {
        return process_single_escape(str[1]);
    }
    return str[0];
}

static void codegen_statement(ASTNode *stmt, LLVMBuilderRef builder,
                       LLVMTypeRef printf_type, LLVMValueRef printf_func,
                       LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                       LLVMValueRef *functions, LLVMTypeRef *function_types,
                       ASTNode **nodes, size_t count,
                       const char *func_ret_type);

// Forward declarations for codegen
static void codegen_block(ASTNode *block, LLVMBuilderRef builder,
                         LLVMTypeRef printf_type, LLVMValueRef printf_func,
                         LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                         LLVMValueRef *functions, LLVMTypeRef *function_types,
                         ASTNode **nodes, size_t count, const char *func_ret_type) {
    for (size_t i = 0; i < block->block.stmt_count; i++) {
        codegen_statement(block->block.statements[i], builder, printf_type, printf_func,
                         putchar_type, putchar_func, functions, function_types, nodes, count, func_ret_type);
    }
}

static void codegen_statement(ASTNode *stmt, LLVMBuilderRef builder,
                              LLVMTypeRef printf_type, LLVMValueRef printf_func,
                              LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                              LLVMValueRef *functions, LLVMTypeRef *function_types,
                              ASTNode **nodes, size_t count, const char *func_ret_type) {
    if (stmt->type == AST_BLOCK) {
        codegen_block(stmt, builder, printf_type, printf_func, putchar_type, putchar_func,
                     functions, function_types, nodes, count, func_ret_type);
    } else if (stmt->type == AST_RETURN) {
        LLVMTypeRef ret_llvm_type = get_llvm_type(func_ret_type);
        
        if (strcmp(stmt->ret.type, "str") == 0) {
            char *processed = process_escapes(stmt->ret.value);
            LLVMValueRef str = LLVMBuildGlobalStringPtr(builder, processed, "str");
            free(processed);
            LLVMBuildRet(builder, str);
        } else if (strcmp(stmt->ret.type, "char") == 0) {
            char ch = process_char_literal(stmt->ret.value);
            LLVMBuildRet(builder, LLVMConstInt(LLVMInt8Type(), (unsigned char)ch, false));
        } else if (strcmp(stmt->ret.type, "int") == 0) {
            if (strcmp(func_ret_type, "i128") == 0 || strcmp(func_ret_type, "u128") == 0) {
                LLVMValueRef val = LLVMConstIntOfString(ret_llvm_type, stmt->ret.value, 10);
                LLVMBuildRet(builder, val);
            } else {
                long long val = atoll(stmt->ret.value);
                LLVMBuildRet(builder, LLVMConstInt(ret_llvm_type, val, false));
            }
        } else if (strcmp(stmt->ret.type, "float") == 0) {
            double val = atof(stmt->ret.value);
            if (strcmp(func_ret_type, "f32") == 0) {
                LLVMBuildRet(builder, LLVMConstReal(LLVMFloatType(), val));
            } else {
                LLVMBuildRet(builder, LLVMConstReal(LLVMDoubleType(), val));
            }
        } else if (strcmp(stmt->ret.type, "bool") == 0) {
            LLVMBuildRet(builder, LLVMConstInt(LLVMInt1Type(), atoi(stmt->ret.value), false));
        }
    } else if (stmt->type == AST_DISPLAY) {
        if (strcmp(stmt->display.type, "string") == 0) {
            char *processed = process_escapes(stmt->display.message);
            LLVMValueRef str = LLVMBuildGlobalStringPtr(builder, processed, "str");
            free(processed);
            LLVMValueRef args[] = { str };
            LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
        } else if (strcmp(stmt->display.type, "char") == 0) {
            char ch = process_char_literal(stmt->display.message);
            LLVMValueRef char_val = LLVMConstInt(LLVMInt32Type(), (unsigned char)ch, false);
            LLVMValueRef args[] = { char_val };
            LLVMBuildCall2(builder, putchar_type, putchar_func, args, 1, "");
        } else if (strcmp(stmt->display.type, "call") == 0) {
            // Find the function being called
            for (size_t k = 0; k < count; k++) {
                if (nodes[k]->type == AST_FUNCTION && 
                    strcmp(nodes[k]->function.name, stmt->display.call_name) == 0) {
                    // Call the function
                    LLVMValueRef result = LLVMBuildCall2(builder, function_types[k], functions[k], NULL, 0, "calltmp");
                    
                    // Check return type and display accordingly
                    const char *ret_type = nodes[k]->function.return_type;
                    if (strcmp(ret_type, "str") == 0) {
                        LLVMValueRef args[] = { result };
                        LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
                    } else if (strcmp(ret_type, "char") == 0) {
                        // Cast i8 to i32 for putchar
                        LLVMValueRef char_as_i32 = LLVMBuildZExt(builder, result, LLVMInt32Type(), "chartoi32");
                        LLVMValueRef args[] = { char_as_i32 };
                        LLVMBuildCall2(builder, putchar_type, putchar_func, args, 1, "");
                    }
                    break;
                }
            }
        }
    } else if (stmt->type == AST_CALL) {
        for (size_t k = 0; k < count; k++) {
            if (nodes[k]->type == AST_FUNCTION && 
                strcmp(nodes[k]->function.name, stmt->call.name) == 0) {
                LLVMBuildCall2(builder, function_types[k], functions[k], NULL, 0, "");
                break;
            }
        }
    }
}

// Code generation and compilation
static void codegen_and_compile(ASTNode **nodes, size_t count, const CompileOptions *opts) {
    LLVMModuleRef module = LLVMModuleCreateWithName("glyth_module");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    
    // Declare printf
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32Type(), printf_args, 1, true);
    LLVMValueRef printf_func = LLVMAddFunction(module, "printf", printf_type);
    
    // Declare putchar
    LLVMTypeRef putchar_args[] = { LLVMInt32Type() };
    LLVMTypeRef putchar_type = LLVMFunctionType(LLVMInt32Type(), putchar_args, 1, false);
    LLVMValueRef putchar_func = LLVMAddFunction(module, "putchar", putchar_type);
    
    // First pass: declare all functions and store their types
    LLVMValueRef *functions = calloc(count, sizeof(LLVMValueRef));
    LLVMTypeRef *function_types = calloc(count, sizeof(LLVMTypeRef));
    
    for (size_t i = 0; i < count; i++) {
        if (nodes[i]->type == AST_FUNCTION) {
            LLVMTypeRef ret_type = get_llvm_type(nodes[i]->function.return_type);
            if (!ret_type) {
                fprintf(stderr, "Unknown return type: %s\n", nodes[i]->function.return_type);
                exit(1);
            }
            LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, false);
            function_types[i] = func_type;
            functions[i] = LLVMAddFunction(module, nodes[i]->function.name, func_type);
        }
    }
    
    // Second pass: generate code
    for (size_t i = 0; i < count; i++) {
        if (nodes[i]->type == AST_FUNCTION) {
            LLVMValueRef func = functions[i];
            LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);
            
            bool has_return = false;
            
            for (size_t j = 0; j < nodes[i]->function.body_count; j++) {
                ASTNode *stmt = nodes[i]->function.body[j];
                codegen_statement(stmt, builder, printf_type, printf_func, putchar_type, putchar_func,
                                functions, function_types, nodes, count,
                                nodes[i]->function.return_type);
                if (stmt->type == AST_RETURN) has_return = true;
            }
            
            // Add implicit return for void functions
            if (!has_return && strcmp(nodes[i]->function.return_type, "void") == 0) {
                LLVMBuildRetVoid(builder);
            }
        }
    }
    
    // Verify module
    char *error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Module verification failed: %s\n", error);
        LLVMDisposeMessage(error);
        exit(1);
    }
    
    // Emit IR if requested
    if (opts->emit_ir) {
        char *ir_file = malloc(strlen(opts->output_file) + 4);
        sprintf(ir_file, "%s.ll", opts->output_file);
        if (LLVMPrintModuleToFile(module, ir_file, &error)) {
            fprintf(stderr, "Failed to write IR: %s\n", error);
            LLVMDisposeMessage(error);
        }
        free(ir_file);
    }
    
    // Emit bitcode if requested
    if (opts->emit_bc) {
        char *bc_file = malloc(strlen(opts->output_file) + 4);
        sprintf(bc_file, "%s.bc", opts->output_file);
        if (LLVMWriteBitcodeToFile(module, bc_file)) {
            fprintf(stderr, "Failed to write bitcode\n");
        }
        free(bc_file);
    }
    
    // Initialize targets
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    
    // Get target triple and create target machine
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, triple);
    
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        fprintf(stderr, "Failed to get target: %s\n", error);
        LLVMDisposeMessage(error);
        exit(1);
    }
    
    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault,
        LLVMRelocPIC,
        LLVMCodeModelDefault
    );
    
    // Emit object file
    char *obj_file = malloc(strlen(opts->output_file) + 3);
    sprintf(obj_file, "%s.o", opts->output_file);
    if (LLVMTargetMachineEmitToFile(machine, module, obj_file, LLVMObjectFile, &error)) {
        fprintf(stderr, "Failed to emit object file: %s\n", error);
        LLVMDisposeMessage(error);
        exit(1);
    }
    
    // Link to create executable
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cc %s -o %s", obj_file, opts->output_file);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to link executable\n");
        exit(1);
    }
    
    // Clean up object file
    remove(obj_file);
    free(obj_file);
    
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    free(functions);
    free(function_types);
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <source_file>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>    Output file (default: a.out)\n");
    fprintf(stderr, "  --emit-ir    Emit LLVM IR (.ll file)\n");
    fprintf(stderr, "  --emit-bc    Emit LLVM bitcode (.bc file)\n");
    fprintf(stderr, "  --emit-all   Emit both IR and bitcode\n");
}

int main(int argc, char **argv) {
    CompileOptions opts = {
        .input_file = NULL,
        .output_file = "a.out",
        .emit_ir = false,
        .emit_bc = false
    };
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
            opts.output_file = argv[++i];
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            opts.emit_ir = true;
        } else if (strcmp(argv[i], "--emit-bc") == 0) {
            opts.emit_bc = true;
        } else if (strcmp(argv[i], "--emit-all") == 0) {
            opts.emit_ir = true;
            opts.emit_bc = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            opts.input_file = argv[i];
        }
    }
    
    if (!opts.input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }
    
    FILE *f = fopen(opts.input_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Could not open file: %s\n", opts.input_file);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *source = malloc(size + 1);
    if (!source) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return 1;
    }
    
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    size_t count;
    ASTNode **ast = parse(source, opts.input_file, &count);
    
    // Type check before code generation
    Cursor cursor = cursor_init(source, opts.input_file);
    type_check(&cursor, ast, count);
    
    codegen_and_compile(ast, count, &opts);
    
    printf("Compilation successful! Output: %s\n", opts.output_file);
    if (opts.emit_ir) printf("  IR: %s.ll\n", opts.output_file);
    if (opts.emit_bc) printf("  Bitcode: %s.bc\n", opts.output_file);
    
    free(source);
    return 0;
}
