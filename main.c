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
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ_EQ,
    TOK_NOT_EQ,
    TOK_LT,
    TOK_LT_EQ,
    TOK_GT,
    TOK_GT_EQ,
    TOK_LPAREN,
    TOK_RPAREN,
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
    AST_BINARY_OP,
    AST_LITERAL,
} ASTNodeType;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
} BinaryOp;

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
    ASTNode *expr;
} ReturnNode;

typedef struct {
    ASTNode *expr;
} DisplayNode;

typedef struct {
    ASTNode **statements;
    size_t stmt_count;
    size_t stmt_capacity;
} BlockNode;

typedef struct {
    BinaryOp op;
    ASTNode *left;
    ASTNode *right;
} BinaryOpNode;

typedef struct {
    char *value;
    char *type; // "int", "float", "string", "char", "bool"
} LiteralNode;

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
        BinaryOpNode binary;
        LiteralNode literal;
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

// Peek ahead n characters
static inline char cursor_peek_ahead(const Cursor *c, int n) {
    if (c->current[n] == '\0') return '\0';
    return c->current[n];
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

// Error reporting (Compatible with Emacs *compilation buffer*)
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
    
    // Two-character operators
    if (ch == '=' && cursor_peek_ahead(c, 1) == '=') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_EQ_EQ;
        return tok;
    }
    
    if (ch == '!' && cursor_peek_ahead(c, 1) == '=') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_NOT_EQ;
        return tok;
    }
    
    if (ch == '<' && cursor_peek_ahead(c, 1) == '=') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_LT_EQ;
        return tok;
    }
    
    if (ch == '>' && cursor_peek_ahead(c, 1) == '=') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_GT_EQ;
        return tok;
    }
    
    if (ch == ':' && cursor_peek_ahead(c, 1) == ':') {
        cursor_advance(c);
        cursor_advance(c);
        tok->type = TOK_COLON_COLON;
        return tok;
    }
    
    // Single-character tokens
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
    
    if (ch == '(') {
        cursor_advance(c);
        tok->type = TOK_LPAREN;
        return tok;
    }
    
    if (ch == ')') {
        cursor_advance(c);
        tok->type = TOK_RPAREN;
        return tok;
    }
    
    if (ch == '.') {
        cursor_advance(c);
        tok->type = TOK_DOT;
        return tok;
    }
    
    if (ch == '+') {
        cursor_advance(c);
        tok->type = TOK_PLUS;
        return tok;
    }
    
    if (ch == '*') {
        cursor_advance(c);
        tok->type = TOK_STAR;
        return tok;
    }
    
    if (ch == '/') {
        cursor_advance(c);
        tok->type = TOK_SLASH;
        return tok;
    }
    
    if (ch == '<') {
        cursor_advance(c);
        tok->type = TOK_LT;
        return tok;
    }
    
    if (ch == '>') {
        cursor_advance(c);
        tok->type = TOK_GT;
        return tok;
    }
    
    // String literals
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
    
    // Character literals
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
    
    // Numbers (handle minus specially to distinguish from operator)
    if (isdigit(ch) || (ch == '-' && isdigit(cursor_peek_ahead(c, 1)))) {
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
    
    // Handle standalone minus
    if (ch == '-') {
        cursor_advance(c);
        tok->type = TOK_MINUS;
        return tok;
    }
    
    // Identifiers and keywords
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

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);

// Create literal node
static ASTNode *create_literal(Parser *p, const char *value, const char *type) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = AST_BINARY_OP; // We'll use a special marker
    node->line = p->current->line;
    node->col = p->current->col;
    node->literal.value = strdup(value);
    node->literal.type = strdup(type);
    return node;
}

// Parse primary expression (literals, parenthesized expressions, type.min/max)
static ASTNode *parse_primary(Parser *p) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->line = p->current->line;
    node->col = p->current->col;
    
    if (p->current->type == TOK_LPAREN) {
        parser_advance(p);
        ASTNode *expr = parse_expression(p);
        parser_expect(p, TOK_RPAREN, "expected ')'");
        free(node);
        return expr;
    }
    
    if (p->current->type == TOK_NUMBER) {
        node->type = AST_LITERAL;
        node->literal.value = strdup(p->current->value);
        node->literal.type = strdup("int");
        parser_advance(p);
        return node;
    }
    
    if (p->current->type == TOK_FLOAT) {
        node->type = AST_LITERAL;
        node->literal.value = strdup(p->current->value);
        node->literal.type = strdup("float");
        parser_advance(p);
        return node;
    }
    
    if (p->current->type == TOK_STRING) {
        node->type = AST_LITERAL;
        node->literal.value = strdup(p->current->value);
        node->literal.type = strdup("string");
        parser_advance(p);
        return node;
    }
    
    if (p->current->type == TOK_CHAR) {
        node->type = AST_LITERAL;
        node->literal.value = strdup(p->current->value);
        node->literal.type = strdup("char");
        parser_advance(p);
        return node;
    }
    
    if (p->current->type == TOK_TRUE) {
        node->type = AST_LITERAL;
        node->literal.value = strdup("1");
        node->literal.type = strdup("bool");
        parser_advance(p);
        return node;
    }
    
    if (p->current->type == TOK_FALSE) {
        node->type = AST_LITERAL;
        node->literal.value = strdup("0");
        node->literal.type = strdup("bool");
        parser_advance(p);
        return node;
    }
    
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
            
            node->type = AST_LITERAL;
            
            if (strcmp(p->current->value, "min") == 0) {
                if (info.is_bool) {
                    node->literal.value = strdup("0");
                    node->literal.type = strdup("bool");
                } else if (info.is_float) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.17g", info.min_float);
                    node->literal.value = strdup(buf);
                    node->literal.type = strdup("float");
                } else if (strcmp(type_name, "i128") == 0) {
                    node->literal.value = strdup("-170141183460469231731687303715884105728");
                    node->literal.type = strdup("int");
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%lld", info.min_int);
                    node->literal.value = strdup(buf);
                    node->literal.type = strdup("int");
                }
            } else if (strcmp(p->current->value, "max") == 0) {
                if (info.is_bool) {
                    node->literal.value = strdup("1");
                    node->literal.type = strdup("bool");
                } else if (info.is_float) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.17g", info.max_float);
                    node->literal.value = strdup(buf);
                    node->literal.type = strdup("float");
                } else if (strcmp(type_name, "i128") == 0) {
                    node->literal.value = strdup("170141183460469231731687303715884105727");
                    node->literal.type = strdup("int");
                } else if (strcmp(type_name, "u128") == 0) {
                    node->literal.value = strdup("340282366920938463463374607431768211455");
                    node->literal.type = strdup("int");
                } else if (info.is_signed) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%lld", info.max_int);
                    node->literal.value = strdup(buf);
                    node->literal.type = strdup("int");
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%llu", info.max_uint);
                    node->literal.value = strdup(buf);
                    node->literal.type = strdup("int");
                }
            } else {
                error_at(p->cursor, "expected 'min' or 'max' after type name");
            }
            
            parser_advance(p);
            free(type_name);
            return node;
        }
        
        free(type_name);
        error_at(p->cursor, "unexpected type in expression");
    }
    
    // Handle function calls (identifiers)
    if (p->current->type == TOK_IDENT) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_CALL;
        node->line = p->current->line;
        node->col = p->current->col;
        node->call.name = strdup(p->current->value);
        parser_advance(p);
        return node;
    }
    
    error_at(p->cursor, "expected expression");
}

// Parse multiplication and division (higher precedence)
static ASTNode *parse_multiplicative(Parser *p) {
    ASTNode *left = parse_primary(p);
    
    while (p->current->type == TOK_STAR || p->current->type == TOK_SLASH) {
        BinaryOp op = (p->current->type == TOK_STAR) ? OP_MUL : OP_DIV;
        int line = p->current->line;
        int col = p->current->col;
        parser_advance(p);
        
        ASTNode *right = parse_primary(p);
        
        ASTNode *binary = malloc(sizeof(ASTNode));
        binary->type = AST_BINARY_OP;
        binary->line = line;
        binary->col = col;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;
        
        left = binary;
    }
    
    return left;
}

// Parse addition and subtraction
static ASTNode *parse_additive(Parser *p) {
    ASTNode *left = parse_multiplicative(p);
    
    while (p->current->type == TOK_PLUS || p->current->type == TOK_MINUS) {
        BinaryOp op = (p->current->type == TOK_PLUS) ? OP_ADD : OP_SUB;
        int line = p->current->line;
        int col = p->current->col;
        parser_advance(p);
        
        ASTNode *right = parse_multiplicative(p);
        
        ASTNode *binary = malloc(sizeof(ASTNode));
        binary->type = AST_BINARY_OP;
        binary->line = line;
        binary->col = col;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;
        
        left = binary;
    }
    
    return left;
}

// Parse comparison operators
static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_additive(p);
    
    while (p->current->type == TOK_LT || p->current->type == TOK_LT_EQ ||
           p->current->type == TOK_GT || p->current->type == TOK_GT_EQ) {
        BinaryOp op;
        switch (p->current->type) {
            case TOK_LT: op = OP_LT; break;
            case TOK_LT_EQ: op = OP_LE; break;
            case TOK_GT: op = OP_GT; break;
            case TOK_GT_EQ: op = OP_GE; break;
            default: op = OP_LT;
        }
        int line = p->current->line;
        int col = p->current->col;
        parser_advance(p);
        
        ASTNode *right = parse_additive(p);
        
        ASTNode *binary = malloc(sizeof(ASTNode));
        binary->type = AST_BINARY_OP;
        binary->line = line;
        binary->col = col;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;
        
        left = binary;
    }
    
    return left;
}

// Parse equality operators
static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);
    
    while (p->current->type == TOK_EQ_EQ || p->current->type == TOK_NOT_EQ) {
        BinaryOp op = (p->current->type == TOK_EQ_EQ) ? OP_EQ : OP_NE;
        int line = p->current->line;
        int col = p->current->col;
        parser_advance(p);
        
        ASTNode *right = parse_comparison(p);
        
        ASTNode *binary = malloc(sizeof(ASTNode));
        binary->type = AST_BINARY_OP;
        binary->line = line;
        binary->col = col;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;
        
        left = binary;
    }
    
    return left;
}

// Parse expression (top level)
static ASTNode *parse_expression(Parser *p) {
    return parse_equality(p);
}

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
        
        node->ret.expr = parse_expression(p);
        return node;
    }
    
    if (p->current->type == TOK_DISPLAY) {
        ASTNode *node = malloc(sizeof(ASTNode));
        node->type = AST_DISPLAY;
        node->line = p->current->line;
        node->col = p->current->col;
        parser_advance(p);
        
        node->display.expr = parse_expression(p);
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

// Get expression type
static const char *get_expr_type(const ASTNode *expr) {
    if (expr->type == AST_LITERAL) {
        return expr->literal.type;
    } else if (expr->type == AST_BINARY_OP) {
        return "computed";
    }
    return "unknown";
}

static void check_binary_op_types(const Cursor *cursor, const ASTNode *node, const char *expected_type) {
    const char *left_type = get_expr_type(node->binary.left);
    const char *right_type = get_expr_type(node->binary.right);
    
    // Only disallow / with strings
    if (node->binary.op == OP_DIV) {
        if (strcmp(left_type, "string") == 0 || strcmp(right_type, "string") == 0) {
            error_at_node(cursor, node, "cannot use / operator with string type");
        }
    }
    
    // For string operations with *, only allow string * int
    if (node->binary.op == OP_MUL) {
        if (strcmp(left_type, "string") == 0 && strcmp(right_type, "string") == 0) {
            error_at_node(cursor, node, "cannot use * operator between two strings");
        }
    }
}

// Recursive type checker for statements
static void check_statement_types(const Cursor *cursor, const ASTNode *func, const ASTNode *stmt);

static void check_return_expr_type(const Cursor *cursor, const ASTNode *func, const ASTNode *ret_stmt) {
    const char *func_type = func->function.return_type;
    ASTNode *expr = ret_stmt->ret.expr;
    
    // For literals, do type checking
    if (expr->type == AST_LITERAL) {
        const char *ret_type = expr->literal.type;
        const char *ret_value = expr->literal.value;
        
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
        if (strcmp(ret_type, "string") == 0 && strcmp(func_type, "str") != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "incompatible types when returning type 'str' but '%s' was expected",
                     func_type);
            error_at_node(cursor, ret_stmt, msg);
        }
        
        if (strcmp(func_type, "str") == 0 && strcmp(ret_type, "string") != 0) {
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
    } else if (expr->type == AST_BINARY_OP) {
        // Check binary operation types
        check_binary_op_types(cursor, expr, func_type);
    }
}

static void check_statement_types(const Cursor *cursor, const ASTNode *func, const ASTNode *stmt) {
    if (stmt->type == AST_RETURN) {
        check_return_expr_type(cursor, func, stmt);
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

// Forward declarations for codegen
static LLVMValueRef codegen_expression(ASTNode *expr, LLVMBuilderRef builder,
                                       LLVMModuleRef module, const char *expected_type);

static void codegen_statement(ASTNode *stmt, LLVMBuilderRef builder,
                              LLVMModuleRef module,
                              LLVMTypeRef printf_type, LLVMValueRef printf_func,
                              LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                              LLVMValueRef *functions, LLVMTypeRef *function_types,
                              ASTNode **nodes, size_t count,
                              const char *func_ret_type);

static void codegen_block(ASTNode *block, LLVMBuilderRef builder,
                          LLVMModuleRef module,
                          LLVMTypeRef printf_type, LLVMValueRef printf_func,
                          LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                          LLVMValueRef *functions, LLVMTypeRef *function_types,
                          ASTNode **nodes, size_t count, const char *func_ret_type) {
    for (size_t i = 0; i < block->block.stmt_count; i++) {
        codegen_statement(block->block.statements[i], builder, module, printf_type, printf_func,
                          putchar_type, putchar_func, functions, function_types, nodes, count, func_ret_type);
    }
}

// Helper to get strlen at runtime
static LLVMValueRef codegen_strlen(LLVMBuilderRef builder, LLVMModuleRef module, LLVMValueRef str) {
    LLVMTypeRef strlen_args[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef strlen_type = LLVMFunctionType(LLVMInt64Type(), strlen_args, 1, false);
    LLVMValueRef strlen_func = LLVMGetNamedFunction(module, "strlen");
    if (!strlen_func) {
        strlen_func = LLVMAddFunction(module, "strlen", strlen_type);
    }
    LLVMValueRef args[] = { str };
    return LLVMBuildCall2(builder, strlen_type, strlen_func, args, 1, "strlen");
}

// Helper to concatenate strings
static LLVMValueRef codegen_strcat(LLVMBuilderRef builder, LLVMModuleRef module,
                                   LLVMValueRef str1, LLVMValueRef str2) {
    // Get lengths
    LLVMValueRef len1 = codegen_strlen(builder, module, str1);
    LLVMValueRef len2 = codegen_strlen(builder, module, str2);
    
    // Total length + 1 for null terminator
    LLVMValueRef total_len = LLVMBuildAdd(builder, len1, len2, "total_len");
    LLVMValueRef alloc_size = LLVMBuildAdd(builder, total_len, LLVMConstInt(LLVMInt64Type(), 1, false), "alloc_size");
    
    // Allocate memory
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMInt64Type()}, 1, false);
    LLVMValueRef malloc_func = LLVMGetNamedFunction(module, "malloc");
    if (!malloc_func) {
        malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    }
    LLVMValueRef result = LLVMBuildCall2(builder, malloc_type, malloc_func,
                                         (LLVMValueRef[]){alloc_size}, 1, "str_result");
    
    // Copy first string
    LLVMTypeRef strcpy_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
    LLVMValueRef strcpy_func = LLVMGetNamedFunction(module, "strcpy");
    if (!strcpy_func) {
        strcpy_func = LLVMAddFunction(module, "strcpy", strcpy_type);
    }
    LLVMBuildCall2(builder, strcpy_type, strcpy_func, (LLVMValueRef[]){result, str1}, 2, "");
    
    // Concatenate second string
    LLVMValueRef strcat_func = LLVMGetNamedFunction(module, "strcat");
    if (!strcat_func) {
        strcat_func = LLVMAddFunction(module, "strcat", strcpy_type);
    }
    LLVMBuildCall2(builder, strcpy_type, strcat_func, (LLVMValueRef[]){result, str2}, 2, "");
    
    return result;
}

// Helper to repeat a string N times (string * int)
static LLVMValueRef codegen_strrepeat(LLVMBuilderRef builder, LLVMModuleRef module,
                                      LLVMValueRef str, LLVMValueRef count) {
    // Get string length
    LLVMValueRef str_len = codegen_strlen(builder, module, str);
    
    // Total length = str_len * count
    LLVMValueRef total_len = LLVMBuildMul(builder, str_len, count, "total_len");
    
    // Allocate memory (total_len + 1 for null terminator)
    LLVMValueRef alloc_size = LLVMBuildAdd(builder, total_len, 
                                           LLVMConstInt(LLVMInt64Type(), 1, false), "alloc_size");
    
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMInt64Type()}, 1, false);
    LLVMValueRef malloc_func = LLVMGetNamedFunction(module, "malloc");
    if (!malloc_func) {
        malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    }
    LLVMValueRef result = LLVMBuildCall2(builder, malloc_type, malloc_func,
                                         (LLVMValueRef[]){alloc_size}, 1, "str_result");
    
    // Set first byte to null terminator
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8Type(), 0, false), result);
    
    // Create loop to concatenate string N times
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
    LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlock(func, "loop.cond");
    LLVMBasicBlockRef loop_body = LLVMAppendBasicBlock(func, "loop.body");
    LLVMBasicBlockRef loop_end = LLVMAppendBasicBlock(func, "loop.end");
    
    // Allocate counter
    LLVMValueRef counter_ptr = LLVMBuildAlloca(builder, LLVMInt64Type(), "counter");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt64Type(), 0, false), counter_ptr);
    
    LLVMBuildBr(builder, loop_cond);
    
    // Loop condition
    LLVMPositionBuilderAtEnd(builder, loop_cond);
    LLVMValueRef counter_val = LLVMBuildLoad2(builder, LLVMInt64Type(), counter_ptr, "counter_val");
    LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntSLT, counter_val, count, "cond");
    LLVMBuildCondBr(builder, cond, loop_body, loop_end);
    
    // Loop body - concatenate string
    LLVMPositionBuilderAtEnd(builder, loop_body);
    LLVMTypeRef strcat_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
    LLVMValueRef strcat_func = LLVMGetNamedFunction(module, "strcat");
    if (!strcat_func) {
        strcat_func = LLVMAddFunction(module, "strcat", strcat_type);
    }
    LLVMBuildCall2(builder, strcat_type, strcat_func, (LLVMValueRef[]){result, str}, 2, "");
    
    // Increment counter
    LLVMValueRef next_counter = LLVMBuildAdd(builder, counter_val, 
                                             LLVMConstInt(LLVMInt64Type(), 1, false), "next");
    LLVMBuildStore(builder, next_counter, counter_ptr);
    LLVMBuildBr(builder, loop_cond);
    
    // After loop
    LLVMPositionBuilderAtEnd(builder, loop_end);
    
    return result;
}

// Helper to remove N characters from end of string (string - int)
static LLVMValueRef codegen_strtrim(LLVMBuilderRef builder, LLVMModuleRef module,
                                    LLVMValueRef str, LLVMValueRef n) {
    // Get string length
    LLVMValueRef str_len = codegen_strlen(builder, module, str);
    
    // New length = max(0, str_len - n)
    LLVMValueRef new_len = LLVMBuildSub(builder, str_len, n, "new_len");
    
    // Clamp to 0 if negative
    LLVMValueRef zero = LLVMConstInt(LLVMInt64Type(), 0, false);
    LLVMValueRef is_negative = LLVMBuildICmp(builder, LLVMIntSLT, new_len, zero, "is_neg");
    new_len = LLVMBuildSelect(builder, is_negative, zero, new_len, "clamped_len");
    
    // Allocate new string
    LLVMValueRef alloc_size = LLVMBuildAdd(builder, new_len, 
                                           LLVMConstInt(LLVMInt64Type(), 1, false), "alloc_size");
    
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMInt64Type()}, 1, false);
    LLVMValueRef malloc_func = LLVMGetNamedFunction(module, "malloc");
    if (!malloc_func) {
        malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    }
    LLVMValueRef result = LLVMBuildCall2(builder, malloc_type, malloc_func,
                                         (LLVMValueRef[]){alloc_size}, 1, "str_result");
    
    // Use strncpy to copy first new_len characters
    LLVMTypeRef strncpy_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                                (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                                LLVMPointerType(LLVMInt8Type(), 0),
                                                                LLVMInt64Type()}, 3, false);
    LLVMValueRef strncpy_func = LLVMGetNamedFunction(module, "strncpy");
    if (!strncpy_func) {
        strncpy_func = LLVMAddFunction(module, "strncpy", strncpy_type);
    }
    LLVMBuildCall2(builder, strncpy_type, strncpy_func, 
                   (LLVMValueRef[]){result, str, new_len}, 3, "");
    
    // Add null terminator
    LLVMValueRef null_pos = LLVMBuildGEP2(builder, LLVMInt8Type(), result, 
                                          (LLVMValueRef[]){new_len}, 1, "null_pos");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8Type(), 0, false), null_pos);
    
    return result;
}

// Helper to add N spaces to the end of a string (string + int)
static LLVMValueRef codegen_strpad(LLVMBuilderRef builder, LLVMModuleRef module,
                                   LLVMValueRef str, LLVMValueRef n) {
    // Get string length
    LLVMValueRef str_len = codegen_strlen(builder, module, str);
    
    // Total length = str_len + n
    LLVMValueRef total_len = LLVMBuildAdd(builder, str_len, n, "total_len");
    
    // Allocate memory (total_len + 1 for null terminator)
    LLVMValueRef alloc_size = LLVMBuildAdd(builder, total_len, 
                                           LLVMConstInt(LLVMInt64Type(), 1, false), "alloc_size");
    
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMInt64Type()}, 1, false);
    LLVMValueRef malloc_func = LLVMGetNamedFunction(module, "malloc");
    if (!malloc_func) {
        malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    }
    LLVMValueRef result = LLVMBuildCall2(builder, malloc_type, malloc_func,
                                         (LLVMValueRef[]){alloc_size}, 1, "str_result");
    
    // Copy original string
    LLVMTypeRef strcpy_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
    LLVMValueRef strcpy_func = LLVMGetNamedFunction(module, "strcpy");
    if (!strcpy_func) {
        strcpy_func = LLVMAddFunction(module, "strcpy", strcpy_type);
    }
    LLVMBuildCall2(builder, strcpy_type, strcpy_func, (LLVMValueRef[]){result, str}, 2, "");
    
    // Fill the rest with spaces
    LLVMValueRef space_start = LLVMBuildGEP2(builder, LLVMInt8Type(), result,
                                             (LLVMValueRef[]){str_len}, 1, "space_start");
    
    // Use memset to fill with spaces
    LLVMTypeRef memset_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMInt32Type(),
                                                               LLVMInt64Type()}, 3, false);
    LLVMValueRef memset_func = LLVMGetNamedFunction(module, "memset");
    if (!memset_func) {
        memset_func = LLVMAddFunction(module, "memset", memset_type);
    }
    LLVMBuildCall2(builder, memset_type, memset_func,
                   (LLVMValueRef[]){space_start, 
                                    LLVMConstInt(LLVMInt32Type(), ' ', false),
                                    n}, 3, "");
    
    // Add null terminator
    LLVMValueRef null_pos = LLVMBuildGEP2(builder, LLVMInt8Type(), result,
                                          (LLVMValueRef[]){total_len}, 1, "null_pos");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8Type(), 0, false), null_pos);
    
    return result;
}

// Helper to remove ALL occurrences of substring from string
static LLVMValueRef codegen_strremove_all(LLVMBuilderRef builder, LLVMModuleRef module,
                                          LLVMValueRef str, LLVMValueRef substr) {
    // Get lengths
    LLVMValueRef str_len = codegen_strlen(builder, module, str);
    LLVMValueRef substr_len = codegen_strlen(builder, module, substr);
    
    // Allocate result buffer (worst case: same size as original)
    LLVMValueRef alloc_size = LLVMBuildAdd(builder, str_len, 
                                           LLVMConstInt(LLVMInt64Type(), 1, false), "alloc");
    
    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMInt64Type()}, 1, false);
    LLVMValueRef malloc_func = LLVMGetNamedFunction(module, "malloc");
    if (!malloc_func) {
        malloc_func = LLVMAddFunction(module, "malloc", malloc_type);
    }
    LLVMValueRef result = LLVMBuildCall2(builder, malloc_type, malloc_func,
                                         (LLVMValueRef[]){alloc_size}, 1, "str_result");
    
    // Initialize result position
    LLVMValueRef result_pos_ptr = LLVMBuildAlloca(builder, LLVMPointerType(LLVMInt8Type(), 0), "result_pos");
    LLVMBuildStore(builder, result, result_pos_ptr);
    
    // Initialize source position
    LLVMValueRef src_pos_ptr = LLVMBuildAlloca(builder, LLVMPointerType(LLVMInt8Type(), 0), "src_pos");
    LLVMBuildStore(builder, str, src_pos_ptr);
    
    // Declare strstr
    LLVMTypeRef strstr_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
    LLVMValueRef strstr_func = LLVMGetNamedFunction(module, "strstr");
    if (!strstr_func) {
        strstr_func = LLVMAddFunction(module, "strstr", strstr_type);
    }
    
    // Create loop blocks
    LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
    LLVMBasicBlockRef loop_start = LLVMAppendBasicBlock(func, "loop_start");
    LLVMBasicBlockRef found_block = LLVMAppendBasicBlock(func, "found");
    LLVMBasicBlockRef not_found_block = LLVMAppendBasicBlock(func, "not_found");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "merge");
    
    LLVMBuildBr(builder, loop_start);
    
    // Loop start: find next occurrence
    LLVMPositionBuilderAtEnd(builder, loop_start);
    LLVMValueRef src_pos = LLVMBuildLoad2(builder, LLVMPointerType(LLVMInt8Type(), 0), 
                                          src_pos_ptr, "src_pos");
    LLVMValueRef found = LLVMBuildCall2(builder, strstr_type, strstr_func,
                                        (LLVMValueRef[]){src_pos, substr}, 2, "found");
    
    LLVMValueRef null_ptr = LLVMConstNull(LLVMPointerType(LLVMInt8Type(), 0));
    LLVMValueRef is_found = LLVMBuildICmp(builder, LLVMIntNE, found, null_ptr, "is_found");
    LLVMBuildCondBr(builder, is_found, found_block, not_found_block);
    
    // Found block: copy part before match, skip match, continue
    LLVMPositionBuilderAtEnd(builder, found_block);
    
    // Calculate how many bytes to copy before the match
    LLVMValueRef copy_len = LLVMBuildPtrDiff2(builder, LLVMInt8Type(), found, src_pos, "copy_len");
    
    // Use memcpy to copy bytes before match
    LLVMTypeRef memcpy_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMInt64Type()}, 3, false);
    LLVMValueRef memcpy_func = LLVMGetNamedFunction(module, "memcpy");
    if (!memcpy_func) {
        memcpy_func = LLVMAddFunction(module, "memcpy", memcpy_type);
    }
    
    LLVMValueRef result_pos = LLVMBuildLoad2(builder, LLVMPointerType(LLVMInt8Type(), 0),
                                             result_pos_ptr, "result_pos");
    LLVMBuildCall2(builder, memcpy_type, memcpy_func,
                   (LLVMValueRef[]){result_pos, src_pos, copy_len}, 3, "");
    
    // Update result position
    LLVMValueRef new_result_pos = LLVMBuildGEP2(builder, LLVMInt8Type(), result_pos,
                                                (LLVMValueRef[]){copy_len}, 1, "new_result_pos");
    LLVMBuildStore(builder, new_result_pos, result_pos_ptr);
    
    // Update source position (skip the matched substring)
    LLVMValueRef new_src_pos = LLVMBuildGEP2(builder, LLVMInt8Type(), found,
                                             (LLVMValueRef[]){substr_len}, 1, "new_src_pos");
    LLVMBuildStore(builder, new_src_pos, src_pos_ptr);
    
    // Continue loop
    LLVMBuildBr(builder, loop_start);
    
    // Not found block: copy remaining string and exit
    LLVMPositionBuilderAtEnd(builder, not_found_block);
    
    LLVMValueRef final_result_pos = LLVMBuildLoad2(builder, LLVMPointerType(LLVMInt8Type(), 0),
                                                   result_pos_ptr, "final_result_pos");
    LLVMValueRef final_src_pos = LLVMBuildLoad2(builder, LLVMPointerType(LLVMInt8Type(), 0),
                                                src_pos_ptr, "final_src_pos");
    
    LLVMTypeRef strcpy_type = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                               (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                               LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
    LLVMValueRef strcpy_func = LLVMGetNamedFunction(module, "strcpy");
    if (!strcpy_func) {
        strcpy_func = LLVMAddFunction(module, "strcpy", strcpy_type);
    }
    LLVMBuildCall2(builder, strcpy_type, strcpy_func,
                   (LLVMValueRef[]){final_result_pos, final_src_pos}, 2, "");
    
    LLVMBuildBr(builder, merge_block);
    
    // Merge block
    LLVMPositionBuilderAtEnd(builder, merge_block);
    
    return result;
}

static LLVMValueRef codegen_expression(ASTNode *expr, LLVMBuilderRef builder,
                                       LLVMModuleRef module, const char *expected_type) {
    // Handle literals
    if (expr->type == AST_LITERAL) {
        const char *type = expr->literal.type;
        const char *value = expr->literal.value;
        
        if (strcmp(type, "string") == 0) {
            char *processed = process_escapes(value);
            LLVMValueRef str = LLVMBuildGlobalStringPtr(builder, processed, "str");
            free(processed);
            return str;
        } else if (strcmp(type, "char") == 0) {
            char ch = process_char_literal(value);
            return LLVMConstInt(LLVMInt8Type(), (unsigned char)ch, false);
        } else if (strcmp(type, "int") == 0) {
            LLVMTypeRef target_type = get_llvm_type(expected_type);
            if (!target_type) target_type = LLVMInt32Type();
            
            if (strcmp(expected_type, "i128") == 0 || strcmp(expected_type, "u128") == 0) {
                return LLVMConstIntOfString(target_type, value, 10);
            } else {
                long long val = atoll(value);
                return LLVMConstInt(target_type, val, false);
            }
        } else if (strcmp(type, "float") == 0) {
            double val = atof(value);
            if (strcmp(expected_type, "f32") == 0) {
                return LLVMConstReal(LLVMFloatType(), val);
            } else {
                return LLVMConstReal(LLVMDoubleType(), val);
            }
        } else if (strcmp(type, "bool") == 0) {
            return LLVMConstInt(LLVMInt1Type(), atoi(value), false);
        }
    }
    
    // Handle binary operations
    if (expr->type == AST_BINARY_OP) {
        const char *left_type = get_expr_type(expr->binary.left);
        const char *right_type = get_expr_type(expr->binary.right);
        
        // Special case: string * int or int * string (repeat)
        if ((strcmp(left_type, "string") == 0 && strcmp(right_type, "int") == 0) ||
            (strcmp(left_type, "int") == 0 && strcmp(right_type, "string") == 0)) {
            if (expr->binary.op == OP_MUL) {
                LLVMValueRef str_val, int_val;
                if (strcmp(left_type, "string") == 0) {
                    str_val = codegen_expression(expr->binary.left, builder, module, expected_type);
                    int_val = codegen_expression(expr->binary.right, builder, module, "i64");
                } else {
                    int_val = codegen_expression(expr->binary.left, builder, module, "i64");
                    str_val = codegen_expression(expr->binary.right, builder, module, expected_type);
                }
                return codegen_strrepeat(builder, module, str_val, int_val);
            }
        }
        
        // Special case: string + int (add spaces) or string - int (trim)
        if (strcmp(left_type, "string") == 0 && strcmp(right_type, "int") == 0) {
            LLVMValueRef str_val = codegen_expression(expr->binary.left, builder, module, expected_type);
            LLVMValueRef int_val = codegen_expression(expr->binary.right, builder, module, "i64");
            
            if (expr->binary.op == OP_ADD) {
                return codegen_strpad(builder, module, str_val, int_val);
            } else if (expr->binary.op == OP_SUB) {
                return codegen_strtrim(builder, module, str_val, int_val);
            }
        }
        
        // Recursively evaluate left and right operands
        LLVMValueRef left = codegen_expression(expr->binary.left, builder, module, expected_type);
        LLVMValueRef right = codegen_expression(expr->binary.right, builder, module, expected_type);
        
        // Determine if we're dealing with strings
        LLVMTypeRef left_llvm_type = LLVMTypeOf(left);
        LLVMTypeRef right_llvm_type = LLVMTypeOf(right);
        bool left_is_ptr = LLVMGetTypeKind(left_llvm_type) == LLVMPointerTypeKind;
        bool right_is_ptr = LLVMGetTypeKind(right_llvm_type) == LLVMPointerTypeKind;
        
        // String operations
        if (left_is_ptr && right_is_ptr && 
            (strcmp(left_type, "string") == 0 || strcmp(left_type, "computed") == 0) &&
            (strcmp(right_type, "string") == 0 || strcmp(right_type, "computed") == 0)) {
            
            switch (expr->binary.op) {
            case OP_ADD:
                return codegen_strcat(builder, module, left, right);
            case OP_SUB:
                return codegen_strremove_all(builder, module, left, right);
            case OP_EQ: {
                LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(),
                                                           (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                                           LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
                LLVMValueRef strcmp_func = LLVMGetNamedFunction(module, "strcmp");
                if (!strcmp_func) {
                    strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);
                }
                LLVMValueRef cmp_result = LLVMBuildCall2(builder, strcmp_type, strcmp_func,
                                                         (LLVMValueRef[]){left, right}, 2, "strcmp");
                return LLVMBuildICmp(builder, LLVMIntEQ, cmp_result,
                                     LLVMConstInt(LLVMInt32Type(), 0, false), "streq");
            }
            case OP_NE: {
                LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32Type(),
                                                           (LLVMTypeRef[]){LLVMPointerType(LLVMInt8Type(), 0),
                                                                           LLVMPointerType(LLVMInt8Type(), 0)}, 2, false);
                LLVMValueRef strcmp_func = LLVMGetNamedFunction(module, "strcmp");
                if (!strcmp_func) {
                    strcmp_func = LLVMAddFunction(module, "strcmp", strcmp_type);
                }
                LLVMValueRef cmp_result = LLVMBuildCall2(builder, strcmp_type, strcmp_func,
                                                         (LLVMValueRef[]){left, right}, 2, "strcmp");
                return LLVMBuildICmp(builder, LLVMIntNE, cmp_result,
                                     LLVMConstInt(LLVMInt32Type(), 0, false), "strne");
            }
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE: {
                // Compare string lengths
                LLVMValueRef len1 = codegen_strlen(builder, module, left);
                LLVMValueRef len2 = codegen_strlen(builder, module, right);
                
                switch (expr->binary.op) {
                case OP_LT: return LLVMBuildICmp(builder, LLVMIntULT, len1, len2, "cmp");
                case OP_LE: return LLVMBuildICmp(builder, LLVMIntULE, len1, len2, "cmp");
                case OP_GT: return LLVMBuildICmp(builder, LLVMIntUGT, len1, len2, "cmp");
                case OP_GE: return LLVMBuildICmp(builder, LLVMIntUGE, len1, len2, "cmp");
                default: break;
                }
            }
            default:
                break;
            }
        }
        
        // Determine operation type category
        bool is_float_op = (strcmp(left_type, "float") == 0 || strcmp(right_type, "float") == 0);
        bool is_bool_op = (strcmp(left_type, "bool") == 0 && strcmp(right_type, "bool") == 0);
        bool is_char_op = (strcmp(left_type, "char") == 0 && strcmp(right_type, "char") == 0);
        bool is_mixed_bool_int = ((strcmp(left_type, "bool") == 0 && strcmp(right_type, "int") == 0) ||
                                  (strcmp(left_type, "int") == 0 && strcmp(right_type, "bool") == 0));
        
        // Boolean operations
        if (is_bool_op) {
            switch (expr->binary.op) {
            case OP_ADD: return LLVMBuildOr(builder, left, right, "or");
            case OP_SUB: return LLVMBuildAnd(builder, left, LLVMBuildNot(builder, right, "not"), "and_not");
            case OP_MUL: return LLVMBuildAnd(builder, left, right, "and");
            case OP_DIV: return LLVMBuildAnd(builder, left, right, "div");
            case OP_EQ: return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
            case OP_NE: return LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
            case OP_LT: return LLVMBuildICmp(builder, LLVMIntULT, left, right, "lt");
            case OP_LE: return LLVMBuildICmp(builder, LLVMIntULE, left, right, "le");
            case OP_GT: return LLVMBuildICmp(builder, LLVMIntUGT, left, right, "gt");
            case OP_GE: return LLVMBuildICmp(builder, LLVMIntUGE, left, right, "ge");
            }
        }
        
        // Mixed bool/int operations
        if (is_mixed_bool_int) {
            LLVMValueRef left_i32, right_i32;
            
            if (strcmp(left_type, "bool") == 0) {
                left_i32 = LLVMBuildZExt(builder, left, LLVMInt32Type(), "bool_to_i32");
                right_i32 = right;
            } else {
                left_i32 = left;
                right_i32 = LLVMBuildZExt(builder, right, LLVMInt32Type(), "bool_to_i32");
            }
            
            switch (expr->binary.op) {
            case OP_ADD: return LLVMBuildAdd(builder, left_i32, right_i32, "add");
            case OP_SUB: return LLVMBuildSub(builder, left_i32, right_i32, "sub");
            case OP_MUL: return LLVMBuildMul(builder, left_i32, right_i32, "mul");
            case OP_DIV: return LLVMBuildSDiv(builder, left_i32, right_i32, "div");
            case OP_EQ: return LLVMBuildICmp(builder, LLVMIntEQ, left_i32, right_i32, "eq");
            case OP_NE: return LLVMBuildICmp(builder, LLVMIntNE, left_i32, right_i32, "ne");
            case OP_LT: return LLVMBuildICmp(builder, LLVMIntSLT, left_i32, right_i32, "lt");
            case OP_LE: return LLVMBuildICmp(builder, LLVMIntSLE, left_i32, right_i32, "le");
            case OP_GT: return LLVMBuildICmp(builder, LLVMIntSGT, left_i32, right_i32, "gt");
            case OP_GE: return LLVMBuildICmp(builder, LLVMIntSGE, left_i32, right_i32, "ge");
            }
        }
        
        // Character operations (treat as unsigned integers)
        if (is_char_op) {
            switch (expr->binary.op) {
            case OP_ADD: return LLVMBuildAdd(builder, left, right, "add");
            case OP_SUB: return LLVMBuildSub(builder, left, right, "sub");
            case OP_MUL: return LLVMBuildMul(builder, left, right, "mul");
            case OP_DIV: return LLVMBuildUDiv(builder, left, right, "div");
            case OP_EQ: return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
            case OP_NE: return LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
            case OP_LT: return LLVMBuildICmp(builder, LLVMIntULT, left, right, "lt");
            case OP_LE: return LLVMBuildICmp(builder, LLVMIntULE, left, right, "le");
            case OP_GT: return LLVMBuildICmp(builder, LLVMIntUGT, left, right, "gt");
            case OP_GE: return LLVMBuildICmp(builder, LLVMIntUGE, left, right, "ge");
            }
        }
        
        // Float operations
        if (is_float_op) {
            switch (expr->binary.op) {
            case OP_ADD: return LLVMBuildFAdd(builder, left, right, "add");
            case OP_SUB: return LLVMBuildFSub(builder, left, right, "sub");
            case OP_MUL: return LLVMBuildFMul(builder, left, right, "mul");
            case OP_DIV: return LLVMBuildFDiv(builder, left, right, "div");
            case OP_EQ: return LLVMBuildFCmp(builder, LLVMRealOEQ, left, right, "eq");
            case OP_NE: return LLVMBuildFCmp(builder, LLVMRealONE, left, right, "ne");
            case OP_LT: return LLVMBuildFCmp(builder, LLVMRealOLT, left, right, "lt");
            case OP_LE: return LLVMBuildFCmp(builder, LLVMRealOLE, left, right, "le");
            case OP_GT: return LLVMBuildFCmp(builder, LLVMRealOGT, left, right, "gt");
            case OP_GE: return LLVMBuildFCmp(builder, LLVMRealOGE, left, right, "ge");
            }
        }
        
        // Integer operations (default)
        bool is_signed = true;
        if (expected_type && expected_type[0] == 'u') {
            is_signed = false;
        }
        
        switch (expr->binary.op) {
        case OP_ADD: return LLVMBuildAdd(builder, left, right, "add");
        case OP_SUB: return LLVMBuildSub(builder, left, right, "sub");
        case OP_MUL: return LLVMBuildMul(builder, left, right, "mul");
        case OP_DIV: 
            return is_signed ? LLVMBuildSDiv(builder, left, right, "div") :
            LLVMBuildUDiv(builder, left, right, "div");
        case OP_EQ: return LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
        case OP_NE: return LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
        case OP_LT: 
            return LLVMBuildICmp(builder, is_signed ? LLVMIntSLT : LLVMIntULT, 
                                 left, right, "lt");
        case OP_LE: 
            return LLVMBuildICmp(builder, is_signed ? LLVMIntSLE : LLVMIntULE, 
                                 left, right, "le");
        case OP_GT: 
            return LLVMBuildICmp(builder, is_signed ? LLVMIntSGT : LLVMIntUGT, 
                                 left, right, "gt");
        case OP_GE: 
            return LLVMBuildICmp(builder, is_signed ? LLVMIntSGE : LLVMIntUGE, 
                                 left, right, "ge");
        }
    }

    // Handle function calls
    if (expr->type == AST_CALL) {
        // Find the function in the module
        LLVMValueRef func = LLVMGetNamedFunction(module, expr->call.name);
        if (!func) {
            fprintf(stderr, "Error: Unknown function: %s\n", expr->call.name);
            exit(1);
        }
        
        // Get the function type
        LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
        
        // Call the function with no arguments
        return LLVMBuildCall2(builder, func_type, func, NULL, 0, "calltmp");
    }

    
    return NULL;
}

static void codegen_statement(ASTNode *stmt, LLVMBuilderRef builder,
                              LLVMModuleRef module,
                              LLVMTypeRef printf_type, LLVMValueRef printf_func,
                              LLVMTypeRef putchar_type, LLVMValueRef putchar_func,
                              LLVMValueRef *functions, LLVMTypeRef *function_types,
                              ASTNode **nodes, size_t count, const char *func_ret_type) {
    if (stmt->type == AST_BLOCK) {
        codegen_block(stmt, builder, module, printf_type, printf_func, putchar_type, putchar_func,
                      functions, function_types, nodes, count, func_ret_type);
    } else if (stmt->type == AST_RETURN) {
        LLVMValueRef ret_val = codegen_expression(stmt->ret.expr, builder, module, func_ret_type);
        LLVMBuildRet(builder, ret_val);
    } else if (stmt->type == AST_DISPLAY) {
        // Evaluate the expression
        const char *expr_type = get_expr_type(stmt->display.expr);
        LLVMValueRef value;
    
        // Handle different expression types
        if (stmt->display.expr->type == AST_LITERAL) {
            if (strcmp(expr_type, "string") == 0) {
                // String literal
                char *processed = process_escapes(stmt->display.expr->literal.value);
                LLVMValueRef str = LLVMBuildGlobalStringPtr(builder, processed, "str");
                free(processed);
                LLVMValueRef args[] = { str };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
                return;
            } else if (strcmp(expr_type, "char") == 0) {
                // Character literal
                char ch = process_char_literal(stmt->display.expr->literal.value);
                LLVMValueRef char_val = LLVMConstInt(LLVMInt32Type(), (unsigned char)ch, false);
                LLVMValueRef args[] = { char_val };
                LLVMBuildCall2(builder, putchar_type, putchar_func, args, 1, "");
                return;
            } else if (strcmp(expr_type, "bool") == 0) {
                // Boolean literal - print "true" or "false"
                const char *bool_str = strcmp(stmt->display.expr->literal.value, "1") == 0 ? "true" : "false";
                LLVMValueRef str = LLVMBuildGlobalStringPtr(builder, bool_str, "bool_str");
                LLVMValueRef args[] = { str };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
                return;
            } else if (strcmp(expr_type, "int") == 0) {
                // Integer literal
                value = codegen_expression(stmt->display.expr, builder, module, "i32");
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%d", "fmt");
                LLVMValueRef args[] = { fmt, value };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
                return;
            } else if (strcmp(expr_type, "float") == 0) {
                // Float literal
                value = codegen_expression(stmt->display.expr, builder, module, "f64");
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%g", "fmt");
                LLVMValueRef args[] = { fmt, value };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
                return;
            }
        } else if (stmt->display.expr->type == AST_CALL) {
            // Function call - need to find function and check return type
            for (size_t k = 0; k < count; k++) {
                if (nodes[k]->type == AST_FUNCTION && 
                    strcmp(nodes[k]->function.name, stmt->display.expr->call.name) == 0) {
                    LLVMValueRef result = LLVMBuildCall2(builder, function_types[k], functions[k], NULL, 0, "calltmp");
                    const char *ret_type = nodes[k]->function.return_type;
                
                    if (strcmp(ret_type, "str") == 0) {
                        LLVMValueRef args[] = { result };
                        LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
                    } else if (strcmp(ret_type, "char") == 0) {
                        LLVMValueRef char_as_i32 = LLVMBuildZExt(builder, result, LLVMInt32Type(), "chartoi32");
                        LLVMValueRef args[] = { char_as_i32 };
                        LLVMBuildCall2(builder, putchar_type, putchar_func, args, 1, "");
                    } else if (strcmp(ret_type, "bool") == 0) {
                        // Print "true" or "false" based on the boolean value
                        LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
                        LLVMBasicBlockRef true_block = LLVMAppendBasicBlock(func, "print_true");
                        LLVMBasicBlockRef false_block = LLVMAppendBasicBlock(func, "print_false");
                        LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "print_merge");
                    
                        LLVMBuildCondBr(builder, result, true_block, false_block);
                    
                        LLVMPositionBuilderAtEnd(builder, true_block);
                        LLVMValueRef true_str = LLVMBuildGlobalStringPtr(builder, "true", "true_str");
                        LLVMBuildCall2(builder, printf_type, printf_func, (LLVMValueRef[]){true_str}, 1, "");
                        LLVMBuildBr(builder, merge_block);
                    
                        LLVMPositionBuilderAtEnd(builder, false_block);
                        LLVMValueRef false_str = LLVMBuildGlobalStringPtr(builder, "false", "false_str");
                        LLVMBuildCall2(builder, printf_type, printf_func, (LLVMValueRef[]){false_str}, 1, "");
                        LLVMBuildBr(builder, merge_block);
                    
                        LLVMPositionBuilderAtEnd(builder, merge_block);
                    } else if (ret_type[0] == 'i' || ret_type[0] == 'u') {
                        // Integer types
                        const char *fmt_str = (ret_type[0] == 'u') ? "%llu" : "%lld";
                        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, fmt_str, "fmt");
                        LLVMValueRef args[] = { fmt, result };
                        LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
                    } else if (ret_type[0] == 'f') {
                        // Float types
                        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%g", "fmt");
                        LLVMValueRef args[] = { fmt, result };
                        LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
                    }
                    return;
                }
            }
        } else if (stmt->display.expr->type == AST_BINARY_OP) {
            // Handle binary operation results
            // Determine the result type based on operands
            const char *result_type = get_expr_type(stmt->display.expr->binary.left);
        
            // Check if it's a comparison operation (returns bool)
            if (stmt->display.expr->binary.op >= OP_EQ && stmt->display.expr->binary.op <= OP_GE) {
                result_type = "bool";
            }
        
            value = codegen_expression(stmt->display.expr, builder, module, result_type);
        
            if (strcmp(result_type, "bool") == 0 || 
                (stmt->display.expr->binary.op >= OP_EQ && stmt->display.expr->binary.op <= OP_GE)) {
                // Print true/false for boolean results
                LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
                LLVMBasicBlockRef true_block = LLVMAppendBasicBlock(func, "print_true");
                LLVMBasicBlockRef false_block = LLVMAppendBasicBlock(func, "print_false");
                LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "print_merge");
            
                LLVMBuildCondBr(builder, value, true_block, false_block);
            
                LLVMPositionBuilderAtEnd(builder, true_block);
                LLVMValueRef true_str = LLVMBuildGlobalStringPtr(builder, "true", "true_str");
                LLVMBuildCall2(builder, printf_type, printf_func, (LLVMValueRef[]){true_str}, 1, "");
                LLVMBuildBr(builder, merge_block);
            
                LLVMPositionBuilderAtEnd(builder, false_block);
                LLVMValueRef false_str = LLVMBuildGlobalStringPtr(builder, "false", "false_str");
                LLVMBuildCall2(builder, printf_type, printf_func, (LLVMValueRef[]){false_str}, 1, "");
                LLVMBuildBr(builder, merge_block);
            
                LLVMPositionBuilderAtEnd(builder, merge_block);
            } else if (strcmp(result_type, "float") == 0) {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%g", "fmt");
                LLVMValueRef args[] = { fmt, value };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
            } else if (strcmp(result_type, "string") == 0 || strcmp(result_type, "computed") == 0) {
                LLVMValueRef args[] = { value };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 1, "");
            } else {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(builder, "%d", "fmt");
                LLVMValueRef args[] = { fmt, value };
                LLVMBuildCall2(builder, printf_type, printf_func, args, 2, "");
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
                codegen_statement(stmt, builder, module, printf_type, printf_func, putchar_type, putchar_func,
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
    fprintf(stderr, "  --/version   Print version number and exit\n");
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
        } else if (strcmp(argv[i], "version") == 0 || strcmp(argv[i], "--version") == 0) {
            fprintf(stderr, "0.0.1\n");
            return 0;
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
