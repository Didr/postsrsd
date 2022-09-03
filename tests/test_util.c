/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "common.h"
#include "util.h"

#include "postsrsd_build_config.h"

#include <check.h>
#include <stdio.h>
#ifdef HAVE_SYS_FILE_H
#    include <sys/file.h>
#endif
#ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#endif
#include <unistd.h>

static char pwd[500];
static char tmpdir[sizeof(pwd) + 7];

void setup_fs()
{
    ck_assert_ptr_nonnull(getcwd(pwd, sizeof(pwd)));
    strcpy(tmpdir, pwd);
    strcat(tmpdir, "/XXXXXX");
    ck_assert_ptr_eq(mkdtemp(tmpdir), tmpdir);
    ck_assert_int_eq(chdir(tmpdir), 0);
}

void teardown_fs()
{
    ck_assert_int_eq(chdir(pwd), 0);
    ck_assert_int_eq(rmdir(tmpdir), 0);
}

START_TEST(util_file_exists)
{
    ck_assert(!file_exists("testfile"));
    ck_assert(!file_exists("testdir"));

    ck_assert_int_eq(mkdir("testdir", 0755), 0);
    FILE* f = fopen("testfile", "w");
    fwrite("Test", 4, 1, f);
    fclose(f);

    ck_assert(file_exists("testfile"));
    ck_assert(!file_exists("testdir"));

    ck_assert_int_eq(unlink("testfile"), 0);
    ck_assert_int_eq(rmdir("testdir"), 0);

    ck_assert(!file_exists("testfile"));
}
END_TEST

START_TEST(util_directory_exists)
{
    ck_assert(!directory_exists("testfile"));
    ck_assert(!directory_exists("testdir"));

    ck_assert_int_eq(mkdir("testdir", 0755), 0);
    FILE* f = fopen("testfile", "w");
    fwrite("Test", 4, 1, f);
    fclose(f);

    ck_assert(directory_exists("testdir"));
    ck_assert(!directory_exists("testfile"));

    ck_assert_int_eq(unlink("testfile"), 0);
    ck_assert_int_eq(rmdir("testdir"), 0);

    ck_assert(!directory_exists("testdir"));
}
END_TEST

START_TEST(util_set_string)
{
    char* s = NULL;
    set_string(&s, strdup("Test"));
    ck_assert_str_eq(s, "Test");
    set_string(&s, NULL);
    ck_assert_ptr_null(s);
}
END_TEST

START_TEST(util_dotlock)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
    for(int i = 0; i < 2; ++i)
    {
        int handle = acquire_lock("testfile");
        ck_assert_int_gt(handle, 0);
        ck_assert_int_lt(acquire_lock("testfile"), 0);
        release_lock("testfile", handle);
    }
#endif
}
END_TEST

START_TEST(util_domain_set)
{
    struct domain_set* D = domain_set_create();
    ck_assert(!domain_set_contains(D, "example.com"));
    ck_assert(!domain_set_contains(D, ".example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, "example.com");
    domain_set_add(D, "www.example.com");
    ck_assert(domain_set_contains(D, "example.com"));
    ck_assert(domain_set_contains(D, "EXAMPLE.COM"));
    ck_assert(domain_set_contains(D, "www.example.com"));
    ck_assert(!domain_set_contains(D, ".example.com"));
    ck_assert(!domain_set_contains(D, "mail.example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, ".example.com");
    ck_assert(domain_set_contains(D, "example.com"));
    ck_assert(domain_set_contains(D, ".example.com"));
    ck_assert(domain_set_contains(D, "www.example.com"));
    ck_assert(domain_set_contains(D, "mail.example.com"));
    ck_assert(!domain_set_contains(D, "exam.com"));
    domain_set_add(D, ".my-0815-examples.com");
    ck_assert(!domain_set_contains(D, "my-0815-examples.com"));
    ck_assert(domain_set_contains(D, "another.one.of.my-0815-examples.com"));
    domain_set_add(D, "invalid$domain.net");
    ck_assert(!domain_set_contains(D, "invalid$domain.net"));
    domain_set_destroy(D);
}

static Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc = tcase_create("test");
    tcase_add_test(tc, util_set_string);
    tcase_add_test(tc, util_domain_set);
    suite_add_tcase(s, tc);
    TCase* tc_fs = tcase_create("fs");
    tcase_add_unchecked_fixture(tc_fs, setup_fs, teardown_fs);
    tcase_add_test(tc_fs, util_file_exists);
    tcase_add_test(tc_fs, util_directory_exists);
    tcase_add_test(tc_fs, util_dotlock);
    suite_add_tcase(s, tc_fs);
    return s;
}

TEST_MAIN(util)
