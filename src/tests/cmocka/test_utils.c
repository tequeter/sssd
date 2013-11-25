/*
    Authors:
        Sumit Bose <sbose@redhat.com>

    Copyright (C) 2013 Red Hat

    SSSD tests: Tests for utility functions

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <popt.h>

#include "tests/cmocka/common_mock.h"

#define TESTS_PATH "tests_utils"
#define TEST_CONF_DB "test_utils_conf.ldb"
#define TEST_SYSDB_FILE "cache_utils_test.ldb"

#define DOM_COUNT 10
#define DOMNAME_TMPL "name_%zu.dom"
#define FLATNAME_TMPL "name_%zu"
#define SID_TMPL "S-1-5-21-1-2-%zu"

struct dom_list_test_ctx {
    size_t dom_count;
    struct sss_domain_info *dom_list;
};

void setup_dom_list(void **state)
{
    struct dom_list_test_ctx *test_ctx;
    struct sss_domain_info *dom = NULL;
    size_t c;

    assert_true(leak_check_setup());

    test_ctx = talloc_zero(global_talloc_context, struct dom_list_test_ctx);
    assert_non_null(test_ctx);

    test_ctx->dom_count = DOM_COUNT;

    for (c = 0; c < test_ctx->dom_count; c++) {
        dom = talloc_zero(test_ctx, struct sss_domain_info);
        assert_non_null(dom);

        dom->name = talloc_asprintf(dom, DOMNAME_TMPL, c);
        assert_non_null(dom->name);

        dom->flat_name = talloc_asprintf(dom, FLATNAME_TMPL, c);
        assert_non_null(dom->flat_name);

        dom->domain_id = talloc_asprintf(dom, SID_TMPL, c);
        assert_non_null(dom->domain_id);

        DLIST_ADD(test_ctx->dom_list, dom);
    }

    check_leaks_push(test_ctx);
    *state = test_ctx;
}

void teardown_dom_list(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    if (test_ctx == NULL) {
        DEBUG(SSSDBG_CRIT_FAILURE, ("Type mismatch\n"));
        return;
    }

    assert_true(check_leaks_pop(test_ctx) == true);
    talloc_free(test_ctx);
    assert_true(leak_check_teardown());
}

void test_find_subdomain_by_name_null(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;

    dom = find_subdomain_by_name(NULL, NULL, false);
    assert_null(dom);

    dom = find_subdomain_by_name(test_ctx->dom_list, NULL, false);
    assert_null(dom);

    dom = find_subdomain_by_name(NULL, "test", false);
    assert_null(dom);
}

void test_find_subdomain_by_name(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_name(test_ctx->dom_list, name, false);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        assert_string_equal(flat_name, dom->flat_name);
        assert_string_equal(sid, dom->domain_id);

        dom = find_subdomain_by_name(test_ctx->dom_list, name, true);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        assert_string_equal(flat_name, dom->flat_name);
        assert_string_equal(sid, dom->domain_id);

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, true);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        assert_string_equal(flat_name, dom->flat_name);
        assert_string_equal(sid, dom->domain_id);

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, false);
        assert_null(dom);

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

void test_find_subdomain_by_name_missing_flat_name(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;
    size_t mis;

    mis = test_ctx->dom_count/2;
    assert_true((mis >= 1 && mis < test_ctx->dom_count));

    dom = test_ctx->dom_list;
    for (c = 0; c < mis; c++) {
        assert_non_null(dom);
        dom = dom->next;
    }
    assert_non_null(dom);
    dom->flat_name = NULL;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_name(test_ctx->dom_list, name, true);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        if (c == mis - 1) {
            assert_null(dom->flat_name);
        } else {
            assert_string_equal(flat_name, dom->flat_name);
        }
        assert_string_equal(sid, dom->domain_id);

        dom = find_subdomain_by_name(test_ctx->dom_list, name, false);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        if (c == mis - 1) {
            assert_null(dom->flat_name);
        } else {
            assert_string_equal(flat_name, dom->flat_name);
        }
        assert_string_equal(sid, dom->domain_id);

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, true);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, false);
        assert_null(dom);

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

void test_find_subdomain_by_name_disabled(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;
    size_t mis;

    mis = test_ctx->dom_count/2;
    assert_true((mis >= 1 && mis < test_ctx->dom_count));

    dom = test_ctx->dom_list;
    for (c = 0; c < mis; c++) {
        assert_non_null(dom);
        dom = dom->next;
    }
    assert_non_null(dom);
    dom->disabled = true;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_name(test_ctx->dom_list, name, true);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        dom = find_subdomain_by_name(test_ctx->dom_list, name, false);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, true);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        dom = find_subdomain_by_name(test_ctx->dom_list, flat_name, false);
        assert_null(dom);

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

void test_find_subdomain_by_sid_null(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;

    dom = find_subdomain_by_sid(NULL, NULL);
    assert_null(dom);

    dom = find_subdomain_by_sid(test_ctx->dom_list, NULL);
    assert_null(dom);

    dom = find_subdomain_by_sid(NULL, "S-1-5-21-1-2-3");
    assert_null(dom);
}

void test_find_subdomain_by_sid(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_sid(test_ctx->dom_list, sid);
        assert_non_null(dom);
        assert_string_equal(name, dom->name);
        assert_string_equal(flat_name, dom->flat_name);
        assert_string_equal(sid, dom->domain_id);

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

void test_find_subdomain_by_sid_missing_sid(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;
    size_t mis;

    mis = test_ctx->dom_count/2;
    assert_true((mis >= 1 && mis < test_ctx->dom_count));

    dom = test_ctx->dom_list;
    for (c = 0; c < mis; c++) {
        assert_non_null(dom);
        dom = dom->next;
    }
    assert_non_null(dom);
    dom->domain_id = NULL;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_sid(test_ctx->dom_list, sid);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

void test_find_subdomain_by_sid_disabled(void **state)
{
    struct dom_list_test_ctx *test_ctx = talloc_get_type(*state,
                                                      struct dom_list_test_ctx);
    struct sss_domain_info *dom;
    size_t c;
    char *name;
    char *flat_name;
    char *sid;
    size_t mis;

    mis = test_ctx->dom_count/2;
    assert_true((mis >= 1 && mis < test_ctx->dom_count));

    dom = test_ctx->dom_list;
    for (c = 0; c < mis; c++) {
        assert_non_null(dom);
        dom = dom->next;
    }
    assert_non_null(dom);
    dom->disabled = true;

    for (c = 0; c < test_ctx->dom_count; c++) {
        name = talloc_asprintf(global_talloc_context, DOMNAME_TMPL, c);
        assert_non_null(name);

        flat_name = talloc_asprintf(global_talloc_context, FLATNAME_TMPL, c);
        assert_non_null(flat_name);

        sid = talloc_asprintf(global_talloc_context, SID_TMPL, c);
        assert_non_null(sid);

        dom = find_subdomain_by_sid(test_ctx->dom_list, sid);
        if (c == mis - 1) {
            assert_null(dom);
        } else {
            assert_non_null(dom);
            assert_string_equal(name, dom->name);
            assert_string_equal(flat_name, dom->flat_name);
            assert_string_equal(sid, dom->domain_id);
        }

        talloc_free(name);
        talloc_free(flat_name);
        talloc_free(sid);
    }
}

struct name_init_test_ctx {
    struct confdb_ctx *confdb;
};

#define GLOBAL_FULL_NAME_FORMAT "%1$s@%2$s"
#define GLOBAL_RE_EXPRESSION "(?P<name>[^@]+)@?(?P<domain>[^@]*$)"

#define TEST_DOMAIN_NAME "test.dom"
#define DOMAIN_FULL_NAME_FORMAT "%3$s\\%1$s"
#define DOMAIN_RE_EXPRESSION "(((?P<domain>[^\\\\]+)\\\\(?P<name>.+$))|" \
                             "((?P<name>[^@]+)@(?P<domain>.+$))|" \
                             "(^(?P<name>[^@\\\\]+)$))"

void confdb_test_setup(void **state)
{
    struct name_init_test_ctx *test_ctx;
    char *conf_db = NULL;
    char *dompath = NULL;
    int ret;
    const char *val[2];
    val[1] = NULL;

    assert_true(leak_check_setup());

    test_ctx = talloc_zero(global_talloc_context, struct name_init_test_ctx);
    assert_non_null(test_ctx);

    conf_db = talloc_asprintf(test_ctx, "%s/%s", TESTS_PATH, TEST_CONF_DB);
    assert_non_null(conf_db);

    ret = confdb_init(test_ctx, &test_ctx->confdb, conf_db);
    assert_int_equal(ret, EOK);

    talloc_free(conf_db);

    val[0] = TEST_DOMAIN_NAME;
    ret = confdb_add_param(test_ctx->confdb, true,
                           "config/sssd", "domains", val);
    assert_int_equal(ret, EOK);

    val[0] = GLOBAL_FULL_NAME_FORMAT;
    ret = confdb_add_param(test_ctx->confdb, true,
                           "config/sssd", "full_name_format", val);
    assert_int_equal(ret, EOK);

    val[0] = GLOBAL_RE_EXPRESSION;
    ret = confdb_add_param(test_ctx->confdb, true,
                           "config/sssd", "re_expression", val);
    assert_int_equal(ret, EOK);

    dompath = talloc_asprintf(test_ctx, "config/domain/%s", TEST_DOMAIN_NAME);
    assert_non_null(dompath);

    val[0] = "ldap";
    ret = confdb_add_param(test_ctx->confdb, true,
                           dompath, "id_provider", val);
    assert_int_equal(ret, EOK);

    val[0] = DOMAIN_FULL_NAME_FORMAT;
    ret = confdb_add_param(test_ctx->confdb, true,
                           dompath, "full_name_format", val);
    assert_int_equal(ret, EOK);

    val[0] = DOMAIN_RE_EXPRESSION;
    ret = confdb_add_param(test_ctx->confdb, true,
                           dompath, "re_expression", val);
    assert_int_equal(ret, EOK);

    talloc_free(dompath);

    check_leaks_push(test_ctx);
    *state = test_ctx;
}

void confdb_test_teardown(void **state)
{
    struct name_init_test_ctx *test_ctx;

    test_ctx = talloc_get_type(*state, struct name_init_test_ctx);

    assert_true(check_leaks_pop(test_ctx) == true);
    talloc_free(test_ctx);
    assert_true(leak_check_teardown());
}

void test_sss_names_init(void **state)
{
    struct name_init_test_ctx *test_ctx;
    struct sss_names_ctx *names_ctx;
    int ret;

    test_ctx = talloc_get_type(*state, struct name_init_test_ctx);

    ret = sss_names_init(test_ctx, test_ctx->confdb, NULL, &names_ctx);
    assert_int_equal(ret, EOK);
    assert_non_null(names_ctx);
    assert_string_equal(names_ctx->re_pattern, GLOBAL_RE_EXPRESSION);
    assert_string_equal(names_ctx->fq_fmt, GLOBAL_FULL_NAME_FORMAT"%3$s");
    assert_int_equal(names_ctx->fq_flags, FQ_FMT_NAME|FQ_FMT_DOMAIN);

    talloc_free(names_ctx);

    ret = sss_names_init(test_ctx, test_ctx->confdb, TEST_DOMAIN_NAME,
                         &names_ctx);
    assert_int_equal(ret, EOK);
    assert_non_null(names_ctx);
    assert_string_equal(names_ctx->re_pattern, DOMAIN_RE_EXPRESSION);
    assert_string_equal(names_ctx->fq_fmt, DOMAIN_FULL_NAME_FORMAT"%2$s");
    assert_int_equal(names_ctx->fq_flags, FQ_FMT_NAME|FQ_FMT_FLAT_NAME);

    talloc_free(names_ctx);
}

void test_well_known_sid_to_name(void **state)
{
    int ret;
    const char *name;
    const char *dom;

    ret = well_known_sid_to_name(NULL, NULL, NULL);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("abc", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-0", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-0-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-0-0", &dom, &name);
    assert_int_equal(ret, EOK);
    assert_string_equal(dom, "NULL AUTHORITY");
    assert_string_equal(name, "NULL SID");

    ret = well_known_sid_to_name("S-1-0-0-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-6", &dom, &name);
    assert_int_equal(ret, EOK);
    assert_string_equal(dom, "NT AUTHORITY");
    assert_string_equal(name, "SERVICE");

    ret = well_known_sid_to_name("S-1-5-6-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-21", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-21-", &dom, &name);
    assert_int_equal(ret, ENOENT);

    ret = well_known_sid_to_name("S-1-5-21-abc", &dom, &name);
    assert_int_equal(ret, ENOENT);

    ret = well_known_sid_to_name("S-1-5-32", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-32-", &dom, &name);
    assert_int_equal(ret, EINVAL);

    ret = well_known_sid_to_name("S-1-5-32-551", &dom, &name);
    assert_int_equal(ret, EOK);
    assert_string_equal(dom, "BUILTIN");
    assert_string_equal(name, "Backup Operators");

    ret = well_known_sid_to_name("S-1-5-32-551-", &dom, &name);
    assert_int_equal(ret, EINVAL);

}

void test_name_to_well_known_sid(void **state)
{
    int ret;
    const char *sid;

    ret = name_to_well_known_sid(NULL, NULL, NULL);
    assert_int_equal(ret, EINVAL);

    ret = name_to_well_known_sid("abc", "def", &sid);
    assert_int_equal(ret, ENOENT);

    ret = name_to_well_known_sid("", "def", &sid);
    assert_int_equal(ret, ENOENT);

    ret = name_to_well_known_sid("BUILTIN", "def", &sid);
    assert_int_equal(ret, EINVAL);

    ret = name_to_well_known_sid("NT AUTHORITY", "def", &sid);
    assert_int_equal(ret, EINVAL);

    ret = name_to_well_known_sid("LOCAL AUTHORITY", "LOCAL", &sid);
    assert_int_equal(ret, EOK);
    assert_string_equal(sid, "S-1-2-0");

    ret = name_to_well_known_sid(NULL, "LOCAL", &sid);
    assert_int_equal(ret, EINVAL);

    ret = name_to_well_known_sid("BUILTIN", "Cryptographic Operators", &sid);
    assert_int_equal(ret, EOK);
    assert_string_equal(sid, "S-1-5-32-569");

    ret = name_to_well_known_sid("NT AUTHORITY", "DIALUP", &sid);
    assert_int_equal(ret, EOK);
    assert_string_equal(sid, "S-1-5-1");
}

int main(int argc, const char *argv[])
{
    poptContext pc;
    int opt;
    int rv;
    struct poptOption long_options[] = {
        POPT_AUTOHELP
        SSSD_DEBUG_OPTS
        POPT_TABLEEND
    };

    const UnitTest tests[] = {
        unit_test_setup_teardown(test_find_subdomain_by_sid_null,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_sid,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_sid_missing_sid,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_sid_disabled,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_name_null,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_name,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_name_missing_flat_name,
                                 setup_dom_list, teardown_dom_list),
        unit_test_setup_teardown(test_find_subdomain_by_name_disabled,
                                 setup_dom_list, teardown_dom_list),

        unit_test_setup_teardown(test_sss_names_init,
                                 confdb_test_setup, confdb_test_teardown),

        unit_test(test_well_known_sid_to_name),
        unit_test(test_name_to_well_known_sid),
    };

    /* Set debug level to invalid value so we can deside if -d 0 was used. */
    debug_level = SSSDBG_INVALID;

    pc = poptGetContext(argv[0], argc, argv, long_options, 0);
    while((opt = poptGetNextOpt(pc)) != -1) {
        switch(opt) {
        default:
            fprintf(stderr, "\nInvalid option %s: %s\n\n",
                    poptBadOption(pc, 0), poptStrerror(opt));
            poptPrintUsage(pc, stderr, 0);
            return 1;
        }
    }
    poptFreeContext(pc);

    DEBUG_INIT(debug_level);

    /* Even though normally the tests should clean up after themselves
     * they might not after a failed run. Remove the old db to be sure */
    tests_set_cwd();
    test_dom_suite_cleanup(TESTS_PATH, TEST_CONF_DB, TEST_SYSDB_FILE);
    test_dom_suite_setup(TESTS_PATH);

    rv = run_tests(tests);
    if (rv == 0) {
        test_dom_suite_cleanup(TESTS_PATH, TEST_CONF_DB, TEST_SYSDB_FILE);
    }
    return rv;
}