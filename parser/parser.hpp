#pragma once
#include <string>
#include <vector>
#include <memory>

#include "../ir/token.hpp"
#include "../ir/expressions.hpp"
#include "../ir/statements.hpp"

class Parser
{
    std::vector<Token> tokens;
    int current = 0;

    bool atEnd();
    Token currentToken();
    Token advance();
    Token previousToken();
    Token peek();
    Token peekNext();
    Token consume(TokenTypes type, std::string message);
    bool match(std::vector<TokenTypes> types);
    void error(std::string message);

    Expression primary();
    Expression factor();
    Expression term();

    Statement expressionStatement();
    Statement statement();

public:
    void program();
    Parser(std::vector<Token> tokens) : tokens(tokens) {}
    std::vector<std::unique_ptr<Statement>> statements;
};