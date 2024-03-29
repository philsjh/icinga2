%{
/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "config/configcompiler.h"
#include "config/typerule.h"
#include "config/configcompilercontext.h"

using namespace icinga;

#include "config/config_parser.hh"
#include <sstream>

#define YYLTYPE icinga::DebugInfo

#define YY_EXTRA_TYPE ConfigCompiler *
#define YY_USER_ACTION 					\
do {							\
	yylloc->Path = yyextra->GetPath();		\
	yylloc->FirstLine = yylineno;			\
	yylloc->FirstColumn = yycolumn;			\
	yylloc->LastLine = yylineno;			\
	yylloc->LastColumn = yycolumn + yyleng - 1;	\
	yycolumn += yyleng;				\
} while (0);

#define YY_INPUT(buf, result, max_size)			\
do {							\
	result = yyextra->ReadInput(buf, max_size);	\
} while (0)

extern int ignore_newlines;

struct lex_buf {
	char *buf;
	size_t size;
};

static void lb_init(lex_buf *lb)
{
	lb->buf = NULL;
	lb->size = 0;
}

static void lb_cleanup(lex_buf *lb)
{
	free(lb->buf);
}

static void lb_append_char(lex_buf *lb, char new_char)
{
	const size_t block_size = 64;

	size_t old_blocks = (lb->size + (block_size - 1)) / block_size;
	size_t new_blocks = ((lb->size + 1) + (block_size - 1)) / block_size;

	if (old_blocks != new_blocks) {
		char *new_buf = (char *)realloc(lb->buf, new_blocks * block_size);

		if (new_buf == NULL && new_blocks > 0)
			throw std::bad_alloc();

		lb->buf = new_buf;
	}

	lb->size++;
	lb->buf[lb->size - 1] = new_char;
}

static char *lb_steal(lex_buf *lb)
{
	lb_append_char(lb, '\0');

	char *buf = lb->buf;
	lb->buf = NULL;
	lb->size = 0;
	return buf;
}
%}

%option reentrant noyywrap yylineno
%option bison-bridge bison-locations
%option never-interactive nounistd

%x C_COMMENT
%x STRING
%x HEREDOC

%%
	lex_buf string_buf;

\"				{ lb_init(&string_buf); BEGIN(STRING); }

<STRING>\"			{
	BEGIN(INITIAL);

	lb_append_char(&string_buf, '\0');

	yylval->text = lb_steal(&string_buf);

	return T_STRING;
				}

<STRING>\n			{
	std::ostringstream msgbuf;
	msgbuf << "Unterminated string found: " << *yylloc;
	ConfigCompilerContext::GetInstance()->AddMessage(true, msgbuf.str());
	BEGIN(INITIAL);
				}

<STRING>\\[0-7]{1,3}		{
	/* octal escape sequence */
	int result;

	(void) sscanf(yytext + 1, "%o", &result);

	if (result > 0xff) {
		/* error, constant is out-of-bounds */
		std::ostringstream msgbuf;
		msgbuf << "Constant is out-of-bounds: " << yytext << " " << *yylloc;
		ConfigCompilerContext::GetInstance()->AddMessage(true, msgbuf.str());
	}

	lb_append_char(&string_buf, result);
				}

<STRING>\\[0-9]+		{
	/* generate error - bad escape sequence; something
	 * like '\48' or '\0777777'
	 */
	std::ostringstream msgbuf;
	msgbuf << "Bad escape sequence found: " << yytext << " " << *yylloc;
	ConfigCompilerContext::GetInstance()->AddMessage(true, msgbuf.str());
				}

<STRING>\\n			{ lb_append_char(&string_buf, '\n'); }
<STRING>\\t			{ lb_append_char(&string_buf, '\t'); }
<STRING>\\r			{ lb_append_char(&string_buf, '\r'); }
<STRING>\\b			{ lb_append_char(&string_buf, '\b'); }
<STRING>\\f			{ lb_append_char(&string_buf, '\f'); }
<STRING>\\(.|\n)		{ lb_append_char(&string_buf, yytext[1]); }

<STRING>[^\\\n\"]+		{
	char *yptr = yytext;

	while (*yptr)
		lb_append_char(&string_buf, *yptr++);
		       	       }

\{\{\{				{ lb_init(&string_buf); BEGIN(HEREDOC); }

<HEREDOC>\}\}\}			{
	BEGIN(INITIAL);

	lb_append_char(&string_buf, '\0');

	yylval->text = lb_steal(&string_buf);

	return T_STRING;
				}

<HEREDOC>(.|\n)			{ lb_append_char(&string_buf, yytext[0]); }

<INITIAL>{
"/*"				BEGIN(C_COMMENT);
}

<C_COMMENT>{
"*/"				BEGIN(INITIAL);
[^*]				/* ignore comment */
"*"				/* ignore star */
}

<C_COMMENT><<EOF>>              {
		std::ostringstream msgbuf;
		msgbuf << "End-of-file while in comment: " << yytext << " " << *yylloc;
		ConfigCompilerContext::GetInstance()->AddMessage(true, msgbuf.str());
		yyterminate();
		       	        }


\/\/[^\n]*			/* ignore C++-style comments */
#[^\n]*				/* ignore shell-style comments */
[ \t]				/* ignore whitespace */

<INITIAL>{
%type				return T_TYPE;
%dictionary			{ yylval->type = TypeDictionary; return T_TYPE_DICTIONARY; }
%array				{ yylval->type = TypeArray; return T_TYPE_ARRAY; }
%number				{ yylval->type = TypeNumber; return T_TYPE_NUMBER; }
%string				{ yylval->type = TypeString; return T_TYPE_STRING; }
%scalar				{ yylval->type = TypeScalar; return T_TYPE_SCALAR; }
%any				{ yylval->type = TypeAny; return T_TYPE_ANY; }
%name				{ yylval->type = TypeName; return T_TYPE_NAME; }
%validator			{ return T_VALIDATOR; }
%require			{ return T_REQUIRE; }
%attribute			{ return T_ATTRIBUTE; }
%inherits			return T_INHERITS;
object				return T_OBJECT;
template			return T_TEMPLATE;
include				return T_INCLUDE;
include_recursive		return T_INCLUDE_RECURSIVE;
library				return T_LIBRARY;
null				return T_NULL;
partial				return T_PARTIAL;
true				{ yylval->num = 1; return T_NUMBER; }
false				{ yylval->num = 0; return T_NUMBER; }
const				return T_CONST;
apply				return T_APPLY;
to				return T_TO;
where				return T_WHERE;
import				return T_IMPORT;
assign				return T_ASSIGN;
ignore				return T_IGNORE;
function			return T_FUNCTION;
lambda				return T_LAMBDA;
return				return T_RETURN;
\<\<				{ yylval->op = &AExpression::OpShiftLeft; return T_SHIFT_LEFT; }
\>\>				{ yylval->op = &AExpression::OpShiftRight; return T_SHIFT_RIGHT; }
\<=				{ yylval->op = &AExpression::OpLessThanOrEqual; return T_LESS_THAN_OR_EQUAL; }
\>=				{ yylval->op = &AExpression::OpGreaterThanOrEqual; return T_GREATER_THAN_OR_EQUAL; }
==				{ yylval->op = &AExpression::OpEqual; return T_EQUAL; }
!=				{ yylval->op = &AExpression::OpNotEqual; return T_NOT_EQUAL; }
!in				{ yylval->op = &AExpression::OpNotIn; return T_NOT_IN; }
in				{ yylval->op = &AExpression::OpIn; return T_IN; }
&&				{ yylval->op = &AExpression::OpLogicalAnd; return T_LOGICAL_AND; }
\|\|				{ yylval->op = &AExpression::OpLogicalOr; return T_LOGICAL_OR; }
[a-zA-Z_][a-zA-Z0-9\-_]*	{ yylval->text = strdup(yytext); return T_IDENTIFIER; }
\<[^\>]*\>			{ yytext[yyleng-1] = '\0'; yylval->text = strdup(yytext + 1); return T_STRING_ANGLE; }
-?[0-9]+(\.[0-9]+)?ms		{ yylval->num = strtod(yytext, NULL) / 1000; return T_NUMBER; }
-?[0-9]+(\.[0-9]+)?d		{ yylval->num = strtod(yytext, NULL) * 60 * 60 * 24; return T_NUMBER; }
-?[0-9]+(\.[0-9]+)?h		{ yylval->num = strtod(yytext, NULL) * 60 * 60; return T_NUMBER; }
-?[0-9]+(\.[0-9]+)?m		{ yylval->num = strtod(yytext, NULL) * 60; return T_NUMBER; }
-?[0-9]+(\.[0-9]+)?s		{ yylval->num = strtod(yytext, NULL); return T_NUMBER; }
-?[0-9]+(\.[0-9]+)?		{ yylval->num = strtod(yytext, NULL); return T_NUMBER; }
=				{ yylval->op = &AExpression::OpSet; return T_SET; }
\+=				{ yylval->op = &AExpression::OpSetPlus; return T_SET_PLUS; }
-=				{ yylval->op = &AExpression::OpSetMinus; return T_SET_MINUS; }
\*=				{ yylval->op = &AExpression::OpSetMultiply; return T_SET_MULTIPLY; }
\/=				{ yylval->op = &AExpression::OpSetDivide; return T_SET_DIVIDE; }
\+				{ yylval->op = &AExpression::OpAdd; return T_PLUS; }
\-				{ yylval->op = &AExpression::OpSubtract; return T_MINUS; }
\*				{ yylval->op = &AExpression::OpMultiply; return T_MULTIPLY; }
\/				{ yylval->op = &AExpression::OpMultiply; return T_DIVIDE_OP; }
\&				{ yylval->op = &AExpression::OpBinaryAnd; return T_BINARY_AND; }
\|				{ yylval->op = &AExpression::OpBinaryOr; return T_BINARY_OR; }
\<				{ yylval->op = &AExpression::OpLessThan; return T_LESS_THAN; }
\>				{ yylval->op = &AExpression::OpLessThan; return T_GREATER_THAN; }
}

[\r\n]+				{ yycolumn -= strlen(yytext) - 1; if (!ignore_newlines) return T_NEWLINE; }
.				return yytext[0];

%%

void ConfigCompiler::InitializeScanner(void)
{
	yylex_init(&m_Scanner);
	yyset_extra(this, m_Scanner);
}

void ConfigCompiler::DestroyScanner(void)
{
	yylex_destroy(m_Scanner);
}
