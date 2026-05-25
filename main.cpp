#ifdef _WIN32
#pragma execution_character_set("utf-8")
#include <windows.h>
#endif

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

/**
 * @brief Типы бинарных и унарных операций в регулярных выражениях
 */
enum class RegexOp { UNION, CONCAT, ITERATION, KLEENE_STAR };

/**
 * @struct RegexNode
 * @brief Узел абстрактного синтаксического дерева (AST) для регулярных выражений.
 *
 * Представляет собой базовый элемент грамматики: терминал, нетерминал,
 * пустое множество или операцию над ними.
 */
struct RegexNode {
    enum Type { SYMBOL, BINARY_OP, UNARY_OP, EMPTY };

    Type type;                      ///< Тип узла
    RegexOp op;                     ///< Тип операции (если узел - операция)
    string symbol;                  ///< Строковое значение (для SYMBOL)
    shared_ptr<RegexNode> left;     ///< Левый потомок
    shared_ptr<RegexNode> right;    ///< Правый потомок

    static shared_ptr<RegexNode> makeSymbol(string s) {
        auto n = make_shared<RegexNode>();
        n->type = SYMBOL; n->symbol = s; return n;
    }
    static shared_ptr<RegexNode> makeEmpty() {
        auto n = make_shared<RegexNode>();
        n->type = EMPTY; return n;
    }
    static shared_ptr<RegexNode> makeBinary(RegexOp o, shared_ptr<RegexNode> l, shared_ptr<RegexNode> r) {
        auto n = make_shared<RegexNode>();
        n->type = BINARY_OP; n->op = o; n->left = l; n->right = r; return n;
    }
    static shared_ptr<RegexNode> makeUnary(RegexOp o, shared_ptr<RegexNode> child) {
        auto n = make_shared<RegexNode>();
        n->type = UNARY_OP; n->op = o; n->right = child; return n;
    }

    /**
     * @brief Конвертирует AST обратно в строковое представление
     * @param isRoot Является ли текущий узел корневым (для расстановки скобок)
     * @return Строковое представление регулярного выражения
     */
    string toString(bool isRoot = true) const {
        if (type == EMPTY) return "пусто";
        if (type == SYMBOL) return symbol;
        if (type == UNARY_OP && op == RegexOp::KLEENE_STAR) {
            bool needParens = (right->type == BINARY_OP);
            if (needParens) return "(" + right->toString(false) + ")*";
            return right->toString(false) + "*";
        }
        if (type == BINARY_OP) {
            string o = (op == RegexOp::UNION) ? " ; " : (op == RegexOp::CONCAT) ? ", " : " # ";
            string res = left->toString(false) + o + right->toString(false);
            if (!isRoot && op != RegexOp::CONCAT) return "(" + res + ")";
            return res;
        }
        return "";
    }

    /**
     * @brief Проверяет наличие нетерминала в поддереве
     */
    bool contains(const string& nt) const {
        if (type == SYMBOL) return symbol == nt;
        if (type == UNARY_OP) return right->contains(nt);
        if (type == BINARY_OP) return left->contains(nt) || right->contains(nt);
        return false;
    }
};

/**
 * @struct Rule
 * @brief Правило контекстно-свободной грамматики
 */
struct Rule {
    string lhs;                   ///< Левая часть правила (Нетерминал)
    shared_ptr<RegexNode> rhs;    ///< Правая часть правила (AST)
};

/**
 * @class Parser
 * @brief Синтаксический анализатор (Парсер) регулярных выражений
 */
class Parser {
public:
    /**
     * @brief Разбирает строку правила в AST
     * @param line Строка вида "A : expr."
     * @return Структура Rule с построенным AST
     */
    static Rule parseRule(const string& line) {
        size_t colon = line.find(':');
        size_t dot = line.rfind('.');
        string lhs = trim(line.substr(0, colon));
        string rhsStr = trim(line.substr(colon + 1, dot - colon - 1));
        return { lhs, parseRegex(rhsStr) };
    }

private:
    static string trim(string s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
        return s;
    }

    static shared_ptr<RegexNode> parseRegex(string expr) {
        expr = trim(expr);
        if (expr.empty() || expr == "пусто" || expr == "e") return RegexNode::makeEmpty();

        if (expr.front() == '(' && expr.back() == ')') {
            int depth = 0; bool ok = true;
            for (size_t i = 0; i < expr.size() - 1; i++) {
                if (expr[i] == '(') depth++;
                else if (expr[i] == ')') depth--;
                if (depth == 0) { ok = false; break; }
            }
            if (ok) return parseRegex(expr.substr(1, expr.size() - 2));
        }

        auto splitByOp = [](const string& s, char opChar) {
            vector<string> parts; string curr; int depth = 0;
            for (char c : s) {
                if (c == '(') depth++; else if (c == ')') depth--;
                if (c == opChar && depth == 0) { parts.push_back(curr); curr = ""; }
                else curr += c;
            }
            parts.push_back(curr);
            return parts;
            };

        auto unions = splitByOp(expr, ';');
        if (unions.size() > 1) {
            auto node = parseRegex(unions[0]);
            for (size_t i = 1; i < unions.size(); i++)
                node = RegexNode::makeBinary(RegexOp::UNION, node, parseRegex(unions[i]));
            return node;
        }

        auto concats = splitByOp(expr, ',');
        if (concats.size() > 1) {
            auto node = parseRegex(concats[0]);
            for (size_t i = 1; i < concats.size(); i++)
                node = RegexNode::makeBinary(RegexOp::CONCAT, node, parseRegex(concats[i]));
            return node;
        }

        if (expr.back() == '*') {
            return RegexNode::makeUnary(RegexOp::KLEENE_STAR, parseRegex(expr.substr(0, expr.size() - 1)));
        }

        return RegexNode::makeSymbol(expr);
    }
};

/**
 * @class RecursionEliminator
 * @brief Класс для устранения левой, правой и центральной рекурсии
 */
class RecursionEliminator {
public:
    /**
     * @brief Основной алгоритм преобразования правила
     * @param rule Исходное рекурсивное правило
     * @return Эквивалентное нерекурсивное правило
     */
    Rule process(const Rule& rule) {
        string A = rule.lhs;
        cout << "\nисходное правило - " << A << ": " << rule.rhs->toString() << ".\n";

        vector<shared_ptr<RegexNode>> fragments = getUnionParts(rule.rhs);

        shared_ptr<RegexNode> r11 = nullptr;
        shared_ptr<RegexNode> r12 = nullptr;
        shared_ptr<RegexNode> r21 = nullptr;
        shared_ptr<RegexNode> r22 = nullptr;

        cout << "Анализ фрагментов:\n";

        for (auto frag : fragments) {
            vector<shared_ptr<RegexNode>> seq = getConcatParts(frag);

            bool startsWithA = (seq.front()->type == RegexNode::SYMBOL && seq.front()->symbol == A);
            bool endsWithA = (seq.back()->type == RegexNode::SYMBOL && seq.back()->symbol == A);

            if (startsWithA && endsWithA && seq.size() >= 3) {
                r11 = extractMiddle(seq);
                cout << "A1 : " << frag->toString() << " -> r11 = " << r11->toString() << "\n";
            }
            else if (startsWithA && !endsWithA && seq.size() >= 2) {
                r12 = extractTail(seq);
                cout << "A2 : " << frag->toString() << " -> r12 = " << r12->toString() << "\n";
            }
            else if (!startsWithA && endsWithA && seq.size() >= 2) {
                r21 = extractHead(seq);
                cout << "A3 : " << frag->toString() << " -> r21 = " << r21->toString() << "\n";
            }
            else if (!frag->contains(A)) {
                if (!r22) r22 = frag;
                else r22 = RegexNode::makeBinary(RegexOp::UNION, r22, frag);
            }
        }

        if (!r11) cout << "A1 : отсутствует ( r11 = пусто )\n";
        if (!r12) cout << "A2 : отсутствует ( r12 = пусто )\n";
        if (!r21) cout << "A3 : отсутствует ( r21 = пусто )\n";
        cout << "A4 : r22 = " << (r22 ? r22->toString() : "пусто") << "\n\n";

        auto empty = RegexNode::makeEmpty();
        if (!r11) r11 = empty;
        if (!r12) r12 = empty;
        if (!r21) r21 = empty;
        if (!r22) r22 = empty;

        auto p1 = (r21->type != RegexNode::EMPTY) ? RegexNode::makeUnary(RegexOp::KLEENE_STAR, r21) : empty;
        auto p2 = (r12->type != RegexNode::EMPTY) ? RegexNode::makeUnary(RegexOp::KLEENE_STAR, r12) : empty;

        auto left_concat = RegexNode::makeBinary(RegexOp::CONCAT, p1, r22);
        auto full_concat = RegexNode::makeBinary(RegexOp::CONCAT, left_concat, p2);
        auto raw_result = RegexNode::makeBinary(RegexOp::ITERATION, full_concat, r11);

        cout << "Применение формулы: " << A << " : " << raw_result->toString() << "\n\n";

        auto simplified_result = simplify(raw_result);
        cout << "Упрощение: " << A << " : " << simplified_result->toString() << "\n";
        cout << "Результат: " << A << " : " << simplified_result->toString() << ".\n";

        return { A, simplified_result };
    }

private:
    vector<shared_ptr<RegexNode>> getUnionParts(shared_ptr<RegexNode> node) {
        vector<shared_ptr<RegexNode>> res;
        if (node->type == RegexNode::BINARY_OP && node->op == RegexOp::UNION) {
            auto l = getUnionParts(node->left);
            auto r = getUnionParts(node->right);
            res.insert(res.end(), l.begin(), l.end());
            res.insert(res.end(), r.begin(), r.end());
        }
        else { res.push_back(node); }
        return res;
    }

    vector<shared_ptr<RegexNode>> getConcatParts(shared_ptr<RegexNode> node) {
        vector<shared_ptr<RegexNode>> res;
        if (node->type == RegexNode::BINARY_OP && node->op == RegexOp::CONCAT) {
            auto l = getConcatParts(node->left);
            auto r = getConcatParts(node->right);
            res.insert(res.end(), l.begin(), l.end());
            res.insert(res.end(), r.begin(), r.end());
        }
        else { res.push_back(node); }
        return res;
    }

    shared_ptr<RegexNode> extractMiddle(const vector<shared_ptr<RegexNode>>& seq) {
        auto res = seq[1];
        for (size_t i = 2; i < seq.size() - 1; i++)
            res = RegexNode::makeBinary(RegexOp::CONCAT, res, seq[i]);
        return res;
    }

    shared_ptr<RegexNode> extractTail(const vector<shared_ptr<RegexNode>>& seq) {
        auto res = seq[1];
        for (size_t i = 2; i < seq.size(); i++)
            res = RegexNode::makeBinary(RegexOp::CONCAT, res, seq[i]);
        return res;
    }

    shared_ptr<RegexNode> extractHead(const vector<shared_ptr<RegexNode>>& seq) {
        auto res = seq[0];
        for (size_t i = 1; i < seq.size() - 1; i++)
            res = RegexNode::makeBinary(RegexOp::CONCAT, res, seq[i]);
        return res;
    }

    shared_ptr<RegexNode> simplify(shared_ptr<RegexNode> node) {
        if (!node || node->type == RegexNode::SYMBOL || node->type == RegexNode::EMPTY) return node;

        if (node->type == RegexNode::UNARY_OP) {
            auto child = simplify(node->right);
            if (child->type == RegexNode::EMPTY) return child;
            return RegexNode::makeUnary(node->op, child);
        }

        auto l = simplify(node->left);
        auto r = simplify(node->right);

        if (node->op == RegexOp::CONCAT) {
            if (l->type == RegexNode::EMPTY) return r;
            if (r->type == RegexNode::EMPTY) return l;
        }
        if (node->op == RegexOp::ITERATION) {
            if (r->type == RegexNode::EMPTY) return l;
        }

        return RegexNode::makeBinary(node->op, l, r);
    }
};

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif
    cout << "==============================================================\n";
    cout << "АЛГОРИТМ УСТРАНЕНИЯ РЕКУРСИИ В CFR-ГРАММАТИКАХ (Модуль SynGT)\n";
    cout << "==============================================================\n";

    string filename = "C:\\Users\\user\\source\\repos\\Kursovai2\\input.txt";

    ifstream file(filename);
    if (!file.is_open()) {
        filename = "input.txt";
        file.open(filename);
        if (!file.is_open()) {
            cerr << "\nОшибка: Не удалось открыть файл!\n";
            return 1;
        }
    }

    RecursionEliminator eliminator;
    string line;

    while (getline(file, line)) {
        if (line.empty() || line.find("//") == 0 || line.find("#") == 0) continue;

        if (line.find(':') != string::npos) {
            cout << "\n--------------------------------------------------------------\n";
            try {
                Rule originalRule = Parser::parseRule(line);
                eliminator.process(originalRule);
            }
            catch (const exception& e) {
                cout << "Ошибка при разборе правила: " << e.what() << "\n";
            }
        }
    }

    cout << "\n==============================================================\n";
    cout << "Обработка завершена.\n";
    return 0;
}