#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Mock os_malloc to simulate memory exhaustion */
static int malloc_fail_count = 0;
static int malloc_call_count = 0;

void *os_malloc(size_t size) {
    malloc_call_count++;
    if (malloc_fail_count > 0 && malloc_call_count >= malloc_fail_count) {
        return NULL;
    }
    return malloc(size);
}

void os_free(void *ptr) {
    free(ptr);
}

/* Forward declare the function under test */
extern void user_interface_init(void);

START_TEST(test_malloc_null_check_boundary)
{
    /* Invariant: user_interface_init must not dereference NULL pointers
       even when os_malloc returns NULL due to memory exhaustion */
    
    struct {
        int fail_at_call;
        const char *description;
    } test_cases[] = {
        {1, "First malloc fails (hostname allocation)"},
        {2, "Second malloc fails (subsequent allocation)"},
        {0, "All mallocs succeed (valid case)"},
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        malloc_call_count = 0;
        malloc_fail_count = test_cases[i].fail_at_call;
        
        /* Call the function under test - it must handle NULL gracefully */
        user_interface_init();
        
        /* If we reach here without segfault, the invariant holds:
           no NULL dereference occurred */
        ck_assert_msg(1, "No crash on malloc failure at call %d", 
                      test_cases[i].fail_at_call);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_malloc_null_check_boundary);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}