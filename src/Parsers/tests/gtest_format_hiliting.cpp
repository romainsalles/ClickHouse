#include <unordered_set>

#include <Parsers/IAST.h>
#include <Parsers/ParserQuery.h>
#include <Parsers/parseQuery.h>
#include <gtest/gtest.h>
#include <Common/StackTrace.h>

String hilite(const String & s, const char * hilite_type)
{
    return hilite_type + s + DB::IAST::hilite_none;
}

String keyword(const String & s)
{
    return hilite(s, DB::IAST::hilite_keyword);
}

String identifier(const String & s)
{
    return hilite(s, DB::IAST::hilite_identifier);
}

String alias(const String & s)
{
    return hilite(s, DB::IAST::hilite_alias);
}

String op(const String & s)
{
    return hilite(s, DB::IAST::hilite_operator);
}

String function(const String & s)
{
    return hilite(s, DB::IAST::hilite_function);
}

String substitution(const String & s)
{
    return hilite(s, DB::IAST::hilite_substitution);
}

std::vector<const char *> HILITES =
    {
        DB::IAST::hilite_keyword,
        DB::IAST::hilite_identifier,
        DB::IAST::hilite_function,
        DB::IAST::hilite_operator,
        DB::IAST::hilite_alias,
        DB::IAST::hilite_substitution,
        DB::IAST::hilite_none
    };

[[maybe_unused]] const char * consume_hilites(const char ** ptr_ptr)
{
    const char * last_hilite = nullptr;
    while (true)
    {
        bool changed_hilite = false;
        for (const char * hilite : HILITES)
        {
            if (std::string_view(*ptr_ptr).starts_with(hilite))
            {
                *ptr_ptr += strlen(hilite);
                changed_hilite = true;
                last_hilite = hilite;
            }
        }
        if (!changed_hilite)
            break;
    }
    return last_hilite;
}

String remove_hilites(std::string_view string)
{
    const char * ptr = string.begin();
    String string_without_hilites;
    while (true)
    {
        consume_hilites(&ptr);
        if (ptr == string.end())
            return string_without_hilites;
        string_without_hilites += *(ptr++);
    }
}

bool are_equal_with_hilites_removed(std::string_view left, std::string_view right)
{
    return remove_hilites(left) == remove_hilites(right);
}

/*
 * Hilited queries cannot be compared symbol-by-symbol, as there's some frivolousness introduced with the hilites. Specifically:
 * 1. Whitespaces could be hilited with any hilite type.
 * 2. Hilite could or could be not reset with hilite_none before the next hilite, i.e. the following strings a and b are equal:
 *      a. hilite_keyword foo hilite_none hilite_operator +
 *      b. hilite_keyword foo hilite_operator +
 */
bool are_equal_with_hilites(std::string_view left, std::string_view right)
{
    if (!are_equal_with_hilites_removed(left, right))
        return false;

    const char * left_it = left.begin();
    const char * right_it = right.begin();
    const char * left_hilite = DB::IAST::hilite_none;
    const char * right_hilite = DB::IAST::hilite_none;

    while (true)
    {
        // Consume all prefix hilites, update the current hilite to be the last one.
        const char * last_hilite = consume_hilites(&left_it);
        if (last_hilite != nullptr)
            left_hilite = last_hilite;

        last_hilite = consume_hilites(&right_it);
        if (last_hilite != nullptr)
            right_hilite = last_hilite;

        if (left_it == left.end() && right_it == right.end())
            return true;

        if (left_it == left.end() || right_it == right.end())
            return false;

        // Lookup one character.
        // Check characters match.
        // Redundant check, given the hilite-ignorant comparison at the beginning, but let's keep it just in case.
        if (*left_it != *right_it)
            return false;

        // Check hilites match if it's not a whitespace.
        if (!std::isspace(*left_it) && left_hilite != right_hilite)
            return false;

        // Consume one character.
        left_it++;
        right_it++;
    }
}

TEST(FormatHiliting, MetaTestConsumeHilites)
{
    using namespace DB;
    // The order is different from the order in HILITES on purpose.
    String s;
    s += IAST::hilite_keyword;
    s += IAST::hilite_alias;
    s += IAST::hilite_identifier;
    s += IAST::hilite_none;
    s += IAST::hilite_operator;
    s += IAST::hilite_substitution;
    s += IAST::hilite_function;
    s += "test";
    s += IAST::hilite_keyword;
    const char * ptr = s.c_str();
    const char * expected_ptr = strchr(ptr, 't');
    const char * last_hilite = consume_hilites(&ptr);
    ASSERT_EQ(expected_ptr, ptr);
    ASSERT_TRUE(last_hilite != nullptr);
    ASSERT_EQ(IAST::hilite_function, last_hilite);
}

TEST(FormatHiliting, MetaTestRemoveHilites)
{
    using namespace DB;
    String s;
    s += IAST::hilite_keyword;
    s += "te";
    s += IAST::hilite_alias;
    s += IAST::hilite_identifier;
    s += "s";
    s += IAST::hilite_none;
    s += "t";
    s += IAST::hilite_operator;
    s += IAST::hilite_substitution;
    s += IAST::hilite_function;
    ASSERT_EQ("test", remove_hilites(s));
}

TEST(FormatHiliting, MetaTestAreEqualWithHilites)
{
    using namespace DB;
    ASSERT_PRED2(are_equal_with_hilites, "", "");

    for (const char * hilite : HILITES)
    {
        ASSERT_PRED2(are_equal_with_hilites, "", std::string_view(hilite));
        ASSERT_PRED2(are_equal_with_hilites, std::string_view(hilite), "");
    }

    {
        String s;
        s += IAST::hilite_none;
        s += "select";
        s += IAST::hilite_none;
        ASSERT_PRED2(are_equal_with_hilites, s, "select");
    }

    {
        String s;
        s += DB::IAST::hilite_none;
        s += "\n sel";
        s += DB::IAST::hilite_none;
        s += "ect";
        s += DB::IAST::hilite_none;
        ASSERT_PRED2(are_equal_with_hilites, s, "\n select");
    }

    {
        String left;
        left += DB::IAST::hilite_keyword;
        left += "keyword long";
        left += DB::IAST::hilite_none;

        String right;
        right += DB::IAST::hilite_keyword;
        right += "keyword";
        right += DB::IAST::hilite_none;
        right += " ";
        right += DB::IAST::hilite_keyword;
        right += "long";
        ASSERT_PRED2(are_equal_with_hilites, left, right);
    }
}

void compare(const String & query, const String & expected)
{
    using namespace DB;
    ParserQuery parser(query.data() + query.size());
    ASTPtr ast = parseQuery(parser, query, 0, 0);

    WriteBufferFromOwnString write_buffer;
    IAST::FormatSettings settings(write_buffer, true);
    settings.hilite = true;
    ast->format(settings);

    ASSERT_PRED2(are_equal_with_hilites_removed, expected, write_buffer.str());
    ASSERT_PRED2(are_equal_with_hilites, expected, write_buffer.str());
}

TEST(FormatHiliting, SimpleSelect)
{
    String query = "select * from table";

    String expected = keyword("SELECT ") + "* " + keyword("FROM ") + identifier("table");

    compare(query, expected);
}

TEST(FormatHiliting, ASTWithElement)
{
    String query = "with alias as (select * from table) select * from table";

    String expected = keyword("WITH ") + alias("alias ") + keyword("AS ")
             + "(" + keyword("SELECT ") + "* " + keyword("FROM ") + identifier("table") + ") "
             + keyword("SELECT ") + "* " + keyword("FROM ") + identifier("table");

    compare(query, expected);
}

TEST(FormatHiliting, ASTWithAlias)
{
    String query = "select a + 1 as b, b";

    String expected = keyword("SELECT ") + identifier("a ") + op("+ ") + "1 " + keyword("AS ") + alias("b") + ", " + identifier("b");

    compare(query, expected);
}

TEST(FormatHiliting, ASTFunction)
{
    String query = "select * from view(select * from table)";

    String expected = keyword("SELECT ") + "* " + keyword("FROM ")
            + function("view(") + keyword("SELECT ") + "* " + keyword("FROM ") + identifier("table") + function(")");

    compare(query, expected);
}

TEST(FormatHiliting, ASTDictionaryAttributeDeclaration)
{
    String query = "CREATE DICTIONARY name (`Name` ClickHouseDataType DEFAULT '' EXPRESSION rand64() IS_OBJECT_ID)";

    String expected = keyword("CREATE DICTIONARY ") + "name "
            + "(`Name` " + function("ClickHouseDataType ")
            + keyword("DEFAULT ") + "'' "
            + keyword("EXPRESSION ") + function("rand64() ")
            + keyword("IS_OBJECT_ID") + ")";

    compare(query, expected);
}

TEST(FormatHiliting, ASTDictionaryClassSourceKeyword)
{
    String query = "CREATE DICTIONARY name (`Name` ClickHouseDataType DEFAULT '' EXPRESSION rand64() IS_OBJECT_ID) "
                        "SOURCE(FILE(PATH 'path'))";

    String expected = keyword("CREATE DICTIONARY ") + "name "
            + "(`Name` " + function("ClickHouseDataType ")
            + keyword("DEFAULT ") + "'' "
            + keyword("EXPRESSION ") + function("rand64() ")
            + keyword("IS_OBJECT_ID") + ") "
            + keyword("SOURCE") + "(" + keyword("FILE") + "(" + keyword("PATH ") + "'path'))";

    compare(query, expected);
}

TEST(FormatHiliting, ASTKillQueryQuery)
{
    String query = "KILL QUERY ON CLUSTER clustername WHERE user = 'username' SYNC";

    String expected = keyword("KILL QUERY ON CLUSTER ") + "clustername "
            + keyword("WHERE ") + identifier("user ") + op("= ") + "'username' "
            + keyword("SYNC");

    compare(query, expected);
}

TEST(FormatHiliting, ASTCreateQuery)
{
    String query = "CREATE TABLE name AS (SELECT *) COMMENT 'hello'";

    String expected = keyword("CREATE TABLE ") + "name " + keyword("AS (SELECT ") + "*" + keyword(") ")
            + keyword("COMMENT ") + "'hello'";

    compare(query, expected);
}
