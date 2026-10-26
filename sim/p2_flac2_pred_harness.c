/* Harness for fw2/cores/flac2_core.h piece 2 (predictors). stdin lines: "<kind> <order> <shift> <b> <force> <n> <split> c0..c(order-1) x0..x(n-1)"; kind 1 FIXED, 2 LPC. x[0..order-1] are the warm-up samples, the rest residuals.
 * The predictor runs from `order` to n in pieces of `split` samples (slicing must not matter). Output: "<tier>" then the n decoded values. */
#include <stdio.h>
#include <stdlib.h>
#include "flac2_core.h"
int main(void)
{
    int kind; unsigned order, shift, b, n, split; int force;
    static int32_t x[8192];
    while (scanf("%d %u %u %u %d %u %u", &kind, &order, &shift, &b, &force, &n, &split) == 7) {
        int32_t coef[32] = {0};
        for (unsigned j = 0; j < order && kind == 2; j++) { int v; scanf("%d", &v); coef[j] = v; }
        for (unsigned i = 0; i < n; i++) { int v; scanf("%d", &v); x[i] = v; }
        f2_pred_t p = {0};
        if (kind == 1) f2_pred_fixed_set(&p, order); else f2_pred_lpc_set(&p, order, shift, coef, b, force);
        for (unsigned i = order; i < n; i += split) { unsigned e = i + split < n ? i + split : n; f2_pred_run(&p, x, i, e); }
        printf("%d\n", p.tier);
        for (unsigned i = 0; i < n; i++) printf("%d ", x[i]);
        printf("\n");
    }
    return 0;
}
