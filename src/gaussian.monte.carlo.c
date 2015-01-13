#include <R_ext/Applic.h>
#include "common.h"

static double mc_fast_pcor(double *covariance, int ncols, double *u, double *d,
    double *vt, int *errcode);
static double mc_cov(double *xx, double *yy, double xm, double ym, int n);

/* unconditional Monte Carlo simulation for correlation-based tests. */
void c_gauss_mcarlo(double *xx, double *yy, int num, int B, double *res,
    double alpha, int test, double *observed) {

int j = 0, k = 0;
double *yperm = NULL;
double enough = ceil(alpha * B) + 1, xm = 0, ym = 0;
int *perm = NULL, *work = NULL;

  /* allocate the arrays needed by RandomPermutation. */
  perm = alloc1dcont(num);
  work = alloc1dcont(num);

  /* allocate the array for the pemutations. */
  yperm = alloc1dreal(num);

  /* cache the means of the two variables (they are invariant under permutation). */
  for (j = 0; j < num; j++) {

    xm += xx[j];
    ym += yy[j];

  }/*FOR*/

  xm /= num;
  ym /= num;

  /* initialize the random number generator. */
  GetRNGstate();

  /* pick up the observed value of the test statistic, then generate a set of
     random permutations (all variable but the second are fixed) and check how
     many tests are greater (in absolute value) than the original one.*/
  switch(test) {

    case GAUSSIAN_MUTUAL_INFORMATION:
    case LINEAR_CORRELATION:
    case FISHER_Z:
      *observed = mc_cov(xx, yy, xm, ym, num);

      for (j = 0; j < B; j++) {

        RandomPermutation(num, perm, work);

        for (k = 0; k < num; k++)
          yperm[k] = yy[perm[k] - 1];

        if (fabs(mc_cov(xx, yperm, xm, ym, num)) > fabs(*observed)) {

          sequential_counter_check(*res);

        }/*THEN*/

      }/*FOR*/

      break;

  }/*SWITCH*/

  /* compute the observed value for the statistic. */
  switch(test) {

    case GAUSSIAN_MUTUAL_INFORMATION:
      *observed = c_fast_cor(xx, yy, num);
      *observed = - num * log(1 - (*observed) * (*observed));
      break;

    case LINEAR_CORRELATION:
      *observed = c_fast_cor(xx, yy, num);
      break;

    case FISHER_Z:

      /* check whether the sample size is big enough for the transform. */
      if (num - 3 < 1)
        error("sample size too small to compute the Fisher's Z transform.");

      *observed = c_fast_cor(xx, yy, num);
      *observed = log((1 + *observed)/(1 - *observed)) / 2 *
                    sqrt((double)(num) - 3);
      break;

  }/*SWITCH*/

  PutRNGstate();

  /* save the observed p-value. */
  *res /= B;

}/*C_GAUSS_MCARLO*/

/* conditional Monte Carlo simulation for correlation-based tests. */
void c_gauss_cmcarlo(double **column, int ncols, int num, int B,
    double *observed, double *pvalue, double alpha, int test) {

int j = 0, k = 0, errcode = 0, *work = NULL, *perm = NULL;
int error_counter = 0;
double permuted = 0, *yperm = NULL, *yorig = NULL;
double enough = ceil(alpha * B) + 1;
double *mean = NULL, *covariance = NULL, *covariance_backup = NULL;
double *u = NULL, *d = NULL, *vt = NULL;

  /* allocate the matrices needed for the SVD decomposition. */
  u = alloc1dreal(ncols * ncols);
  d = alloc1dreal(ncols);
  vt = alloc1dreal(ncols * ncols);

  /* cache the means of the variables (they are invariant under permutation). */
  mean = alloc1dreal(ncols);

  /* compute the mean values  */
  for (j = 0; j < ncols; j++) {

    for (k = 0 ; k < num; k++)
      mean[j] += column[j][k];

    mean[j] /= num;

  }/*FOR*/

  /* allocate and initialize the covariance matrix. */
  covariance = alloc1dreal(ncols * ncols);
  covariance_backup = alloc1dreal(ncols * ncols);
  c_covmat(column, mean, ncols, num, covariance, 0);
  memcpy(covariance_backup, covariance, ncols * ncols * sizeof(double));

  /* substitute the original data with the fake column that will be permuted. */
  yperm = alloc1dreal(num);
  yorig = column[1];
  memcpy(yperm, yorig, num * sizeof(double));
  column[1] = yperm;

   /* allocate the arrays needed by RandomPermutation. */
  perm = alloc1dcont(num);
  work = alloc1dcont(num);

  /* initialize the random number generator. */
  GetRNGstate();

  /* pick up the observed value of the test statistic, then generate a set of
     random permutations (all variable but the second are fixed) and check how
     many tests are greater (in absolute value) than the original one.*/
  switch(test) {

    case GAUSSIAN_MUTUAL_INFORMATION:
    case LINEAR_CORRELATION:
    case FISHER_Z:
      *observed = mc_fast_pcor(covariance, ncols, u, d, vt, &errcode);

      if (errcode)
        error("an error (%d) occurred in the call to dgesvd().\n", errcode);

      for (j = 0; j < B; j++) {

        /* reset the error flag of the SVD Fortran routine. */
        errcode = 0;

        RandomPermutation(num, perm, work);

        for (k = 0; k < num; k++)
          yperm[k] = yorig[perm[k] - 1];

        /* restore the covariance matrix from the good copy. */
        memcpy(covariance, covariance_backup, ncols * ncols * sizeof(double));
        /* update the relevant covariances. */
        c_update_covmat(column, mean, 1, ncols, num, covariance);

        permuted = mc_fast_pcor(covariance, ncols, u, d, vt, &errcode);

        if (errcode != 0)
          error_counter++;

        if (fabs(permuted) > fabs(*observed)) {

          sequential_counter_check(*pvalue);

        }/*THEN*/

      }/*FOR*/

    if (error_counter > 0)
      warning("unable to compute %d permutations due to errors in dgesvd().\n",
        error_counter);

    break;

  }/*SWITCH*/

  /* compute the observed value for the statistic. */
  switch(test) {

    case GAUSSIAN_MUTUAL_INFORMATION:
      *observed = - num * log(1 - (*observed) * (*observed));
      break;

    case LINEAR_CORRELATION:
      break;

    case FISHER_Z:

      /* check whether the sample size is big enough for the transform. */
      if (num - 1 - ncols < 1)
        error("sample size too small to compute the Fisher's Z transform.");

      *observed = log((1 + *observed)/(1 - *observed)) / 2 *
                    sqrt((double)(num) - 1 - ncols);
      break;

  }/*SWITCH*/

  PutRNGstate();

  /* restore the pointer to the original column. */
  column[1] = yorig;

  /* save the observed p-value. */
  *pvalue /= B;

}/*C_GAUSS_CMCARLO*/

/* compute a (barebone version of) the linear correlation coefficient. */
static double mc_cov(double *xx, double *yy, double xm, double ym, int n) {

int i = 0;
double sum = 0;

  /* compute the actual covariance. */
  for (i = 0; i < n; i++)
    sum += (xx[i] - xm) * (yy[i] - ym);

  return sum;

}/*MC_COV*/

static double mc_fast_pcor(double *covariance, int ncols, double *u, double *d,
    double *vt, int *errcode) {

int i = 0, coord1 = 0, coord2 = 0;
double k11 = 0, k12 = 0, k22 = 0;
double res = 0, tol = MACHINE_TOL, sv_tol = 0;

  c_svd(covariance, u, d, vt, &ncols, &ncols, &ncols, FALSE, errcode);

  if (*errcode != 0)
    return 0;

  /* set the threshold for the singular values as in corpcor. */
  sv_tol = ncols * d[0] * tol * tol;

  /* compute the three elements of the pseudoinverse needed
   * for the partial correlation coefficient. */
  for (i = 0; i < ncols; i++) {

    if (d[i] > sv_tol) {

      coord1 = CMC(0, i, ncols);
      coord2 = CMC(i, 1, ncols);

      k11 += u[coord1] * vt[CMC(i, 0, ncols)] / d[i];
      k12 += u[coord1] * vt[coord2] / d[i];
      k22 += u[CMC(1, i, ncols)] * vt[coord2] / d[i];

    }/*THEN*/

  }/*FOR*/

  /* safety check against "divide by zero" errors. */
  if ((k11 < tol) || (k22 < tol))
    res = 0;
  else
    res = -k12 / sqrt(k11 * k22);

  return res;

}/*MC_FAST_PCOR*/

