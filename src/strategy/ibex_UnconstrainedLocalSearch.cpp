//============================================================================
//                                  I B E X
// File        : ibex_UnconstrainedLocalSearch.cpp
// Author      : Jordan Ninin, Gilles Chabert
// Copyright   : Ecole des Mines de Nantes (France)
// License     : See the LICENSE file
// Created     : Mar 19, 2014
// Last Update : Mar 19, 2014
//============================================================================

#include "ibex_UnconstrainedLocalSearch.h"

using namespace std;

namespace ibex {

UnconstrainedLocalSearch::UnconstrainedLocalSearch(const Function& f, const IntervalVector& box) :
						f(f), box(box), n(f.nb_var()),
						eps(0), sigma(0) /* TMP init */,
						data(n) {

	assert(f.image_dim()==1);
}


UnconstrainedLocalSearch::~UnconstrainedLocalSearch() {

}


void UnconstrainedLocalSearch::update_B_SR1(Matrix& Bk, const Vector& sk, const Vector& gk, const Vector& gk1) {
	Vector r = gk1-gk-Bk*sk;
	double tmp = r*sk;
	// if tmp=0 => sk =0 and gk=gk1. That means that we have converge
	// but due to rounding error in the computation of the stopping criteria,
	// we cannot detect it.
	cout << " [update_B_SR1] r*sk = "<< tmp<<endl;
	if (tmp!=0) Bk += 1/(r*sk)*outer_product(r,r);
}

UnconstrainedLocalSearch::ReturnCode UnconstrainedLocalSearch::minimize(const Vector& x0, Vector& xk, double eps, int max_iter) {
	// parameter for the stopping criterion
	this->eps = eps;
	this->sigma=eps/::sqrt(n+1);

	// parameter to update the trust region radius
	double gamma0 = 0.5;
	double gamma2 = 2;

	// parameters to measure the adequacy between
	// the real function and its quadratic approximation.
	// If >= mu, adequacy is good => the new iteration is kept
	// If >= eta, adequacy is very good => the trust region is expanded.
	// If <mu, the old iteration is kept but the approximation of the
	// Hessian is updated (and more accurate so the next iteration is
	// likely to succeed)
	double mu = 0.25;
	double eta = 0.75;

	// could be initialized to something maybe more relevant
	// for the case where x0 is invalid
	xk = x0;

	Vector xk1=x0; // next iterate
	double fk1;
	Vector gk1(n); // next gradient

	int niter = 0; //number of iteration

	try {
		// Initialize the quadratic approximation at the initial point x0
		// like in the quasi-Newton algorithm
		double fk=f.eval(xk1).mid();
		Vector gk=f.gradient(xk1).mid();
		Matrix Bk=Matrix::eye(n);

		// initialize the current point
		xk=xk1;

		// initialization the trust region radius
		double Delta = 0.1*gk.norm();
		IntervalVector region(n);
		BoolMask I(n);

		while ((niter<max_iter)&&(!stop(xk,gk))) {
			niter++;

			// creates the trust region (intersection with bounding box)
			region=box & (IntervalVector(xk).inflate(Delta));

			// Find the Generalized Cauchy Point
			Vector x_gcp = find_gcp(gk, Bk, xk, region);

			// Compute the active set I
			// TODO: I should be retrieved from the last line search of find_gcp
			for (int i =0; i<n;i++) {
				I[i] = fabs(x_gcp[i]-region[i].lb()) <sigma  || fabs(x_gcp[i]-region[i].ub()) <sigma ;
			}

			// Compute the conjugate gradient
			xk1 = conj_grad(gk,Bk,xk,x_gcp,region,I);
			cout << " [minimize] xk1= " << xk1 << endl;

			// Compute the ration of achieved to predicted reduction in the function
			fk1 = f.eval(xk1).mid();

			// computing m(xk1)-f(xk) = (xk1-xk)^T gk + 1/2 (xk1-xk)^T Bk (xk1-xzk)
			Vector sk = xk1-xk;
			double m =(sk*gk + 0.5* (sk*(Bk*sk)));
			cout << " [minimize] sk= " << sk <<"  m= "<<m<< endl;
			// warning if xk1=xk => sk =0 and m =0.
			// In this case, xk = x_gcp = xk1. That means that we have converge
			// but due to rounding error in the computation of the stopping criteria,
			// we have not detected it. So, we stop the algorithm because
			// it is impossible to reach the required precision eps.
			if (m==0)
				niter=max_iter;
			else {
				double rhok = (fk1-fk)/m;
				cout << " [minimize] rhok= " << rhok << endl;
				assert (rhok>=0);

				// update xk
				if (rhok > mu) {
					gk1 = f.gradient(xk1).mid();
					update_B_SR1(Bk,sk,gk,gk1);
					fk = fk1;
					xk = xk1;
					gk = gk1;
				}
				// update Delta
				if (rhok <= mu) Delta = gamma0*Delta;
				else if (rhok>=eta) Delta = gamma2*Delta;
			}
		}
	} catch(InvalidPointException&) {
		return INVALID_POINT;
	}

	return niter<max_iter ? SUCCESS : TOO_MANY_ITER;
}

bool UnconstrainedLocalSearch::stop(const Vector& z, const Vector& g) {
	int i;
	double res, tmp;

	res = 0.0;
	for (i=0; i<n; i++) {
		tmp = z[i]-g[i];
		if (box[i].ub()<tmp) {
			res += ::pow(box[i].ub()-z[i],2);
		}
		else {
			if (tmp<box[i].lb()) {
				res += ::pow(box[i].lb()-z[i],2);
			}
			else {
				res += ::pow(g[i],2);
			}
		}
	}
	cout << "stop = " <<::sqrt(res)<< endl;
	return ::sqrt(res)<eps;
}

Vector UnconstrainedLocalSearch::find_gcp(const Vector& gk, const Matrix& Bk, const Vector& zk,  const IntervalVector& region) {

	// ====================== STEP 2.0 : initialization ======================

	//cout << " [find_gcp] initial region=" << region << endl;
	// The Cauchy point
	Vector z_gcp = zk;

	// The opposite of the gradient of z->z^T*Bk*z - gk^T z
	Vector g = gk - Bk*zk;

	// Compute a descent direction d
	// that must "point" inside the box
	Vector d(n);

	// If the function decreases wrt the ith dimension
	// and the point zk is very close (less than sigma) to the
	// ith "upper face" of the bounding box then we project
	// the gradient on this face (we nullify the ith component).
	// We apply the symmetric case with the "lower face".
	// The constraint x=ui or x=li is activated.
	for (int i = 0 ; i < n ; i++) {
		if(  fabs(gk[i]) > sigma &&
				((gk[i] < 0 && zk[i] < box[i].ub() - sigma) ||
				 (gk[i] > 0 && zk[i] > box[i].lb() + sigma)) )
			d[i] = -gk[i];
		// else d[i] remains equal to 0.
	}
	cout << " [find_gcp] initial d=" << d << endl;
	// compute f'
	double fp = gk*d;
	cout << " [find_gcp] initial fp=" << fp << endl;

	// compute f''
	double fs = d*(Bk*d);
	cout << " [find_gcp] initial fs=" << fs << endl;


	bool gcp_found = (fp >= -eps);

	try {

		while (!gcp_found) {
			// ====================== STEP 2.1 : Find the next breakpoint ======================

			cout << " [find_gcp] current d=" << d << endl;
			cout << " [find_gcp] current fp=" << fp << endl;
			cout << " [find_gcp] current fs=" << fs << endl;
			LineSearch ls(region,z_gcp,d,data,sigma); // if d~0, an exception is raised

			double deltat = ls.alpha_max();
			cout << " [find_gcp] deltat=" << deltat << endl;

			// check if we are in a corner
			// deltat can be very large even if the norm of the gradient is > eps
			// because once the "large" dimensions are treated, it may only
			// remains directions with d[i] very small (but not less than sigma).
			// In any case, we have deltat <= 1/sigma*diam(region) <= 2*Delta/sigma.

			//		assert(deltat<10*(2*Delta/sigma));

			// ====================== STEP 2.2 : Test whether the GCP has been found ======================

			// The minimum is in the segment [0,deltat]
			if ((fs > 0.0) && (0<-(fp/fs)) && (-(fp/fs)<deltat)) {
				z_gcp -= (fp/fs)*d;

				// Security check: z_gcp may be outside the region because of rounding
				ls.proj(z_gcp);

				gcp_found = true;
			}
			else {

				// ====================== STEP 2.3 : Update line derivatives ======================

				// b = Bk*(\sum_{I[i]==2} di*ei)
				Vector b(n);
				for (int i=0; i<n; i++) {
					if (ls.next_activated(i)) {
						for (int j=0; j<n; j++) b[j]+=d[i]*Bk[j][i];
					}
				}

				// set gcp to the the point on the face
				z_gcp = ls.endpoint();

				// update f'
				fp += deltat*fs - b*z_gcp;

				for (int i=0; i<n; i++) {
					if (ls.next_activated(i)) fp -= d[i]*g[i];
				}

				// update f''
				for (int i=0; i<n; i++) {
					fs -= (ls.next_activated(i) ? b[i]*d[i] : 2.0*b[i]*d[i]);
				}

				// update d and I
				for (int i=0; i<n; i++) {
					if (ls.next_activated(i)) {
						cout << " [find_gcp] activate ctr n°" << i << endl;
						d[i] = 0.0;
					}
				}

				// update the termination condition
				gcp_found = (fp >= -eps); // the minimum is at a breakpoint
			}
		}
	}
	catch(LineSearch::NullDirectionException&) {
		// we are in a corner of the region: we stop with current z_gcp
	}

	// ====================== STEP 2.4 : termination with GCP ======================
	cout << " [find_gcp] z_gcp =" << z_gcp << endl;
	assert(region.contains(z_gcp));

	return z_gcp;
}

double UnconstrainedLocalSearch::get_eta(const Vector& gk, const Vector& zk, const IntervalVector& region, const BoolMask& I) {

	double norm=0;

	for (int i=0; i<n; i++) {
		if (!I[i]) {
			double aux =zk[i]-gk[i];
			if (region[i].ub()<aux) {
				norm += ::pow(region[i].ub()-zk[i],2);
			}
			else {
				if (aux <region[i].lb()) {
					norm += ::pow(region[i].lb()-zk[i],2);
				}
				else {
					norm += ::pow(gk[i],2);
				}
			}
		}
	}

	double sqnorm=::sqrt(norm);
	return sqnorm<0.1? 0.1*norm : sqnorm*norm;
}

Vector UnconstrainedLocalSearch::conj_grad(const Vector& gk, const Matrix& Bk, const Vector& xk, const Vector& x_gcp, const IntervalVector& region, const BoolMask& I) {
	int hn = I.nb_unset(); // the restricted dimension

	cout << " [conj_grad] init x_gcp= " << x_gcp << endl;
	if (hn==0) return x_gcp;  // we are in a corner: nothing to do

	// ====================== STEP 3.0 : Initialization ======================
	Vector x=x_gcp; // next point, initialized to gcp

	// gradient of the quadratic model on zk1
	Vector r = -gk-Bk*(x-xk);

	double eta = get_eta(gk,xk,region,I);

	// Initialization of the conjuguate gradient restricted to the direction not(I[i])
	Vector hp(hn); // the restricted conjugate direction
	Vector hx(hn); // the restricted iterate
	Vector hr(hn); // the restricted gradient
	Vector hy(hn); // temporary vector
	Matrix hB(hn,hn); // the restricted hessian matrix
	IntervalVector hregion(hn); // the restricted region

	// initialization of \hat{B}:
	int p=0, q=0;
	for (int i=0; i<n; i++) {
		if (!I[i]) {
			for (int j=0; j<n; j++) {
				if (!I[j]) {
					hB[p][q] = Bk[i][j];
					q++;
				}
			}
			p++;
			q=0;
		}
	}

	// initialization of \hat{r} and \hat{region}
	p=0;
	for (int i=0; i<n; i++) {
		if (!I[i]) {
			hregion[p] = region[i];
			hr[p] = r[i];
			hx[p] = x[i];
			p++;
		}
	}

	double rho1 = 1.0; 					// norm of the restricted gradient at the previous iterate
	double rho2 = ::pow(hr.norm(),2); 	// norm of the restricted gradient

	// ====================== STEP 3.1 : Test for the required accuracy in c.g. iteration ======================
	bool cond = (rho2>::pow(eta,2));

	try {
		while (cond) {

			// ====================== STEP 3.2 : Conjugate gradient recurrences ======================

			// Update the restricted conjugate direction
			// \hat{p} = \hat{r} +beta* \hat{p}
			hp = hr + (rho2/rho1)*hp;

			// Update the temporary vector
			// \hat{y} = \hat{Bk}*\hat{p}
			hy = hB*hp;

			cout << " [conj_grad] current hp=" << hp << endl;
			LineSearch ls(hregion,hx,hp,data,sigma);

			double alpha1=ls.alpha_max();
			cout << " [conj_grad] alpha1=" << alpha1 << endl;

			// first termination condition
			// we check if the hessian is "positive definite for \hat{p}"
			// otherwise the quadratic approximation is concave
			// and the solution if on the border of the region
			double aux = hp*hy;
			if ( aux <= 0) {
				cond = false;
				hx = ls.endpoint();
			}
			else {
				// second termination condition alpha2>alpha1
				double alpha2 = rho2/aux;

				if (alpha2>alpha1) {
					cond = false;
					hx = ls.endpoint();
				}
				else {
					// otherwise update x, r=\hat{r}, hy=y, rho1 and rho2= \hat{r}*\hat{r}=r*r
					hx += alpha2*hp;
					ls.proj(hx);
					hr -= alpha2*hy;
					rho1 = rho2;
					rho2 = hr*hr;
					cond = (rho2>(::pow(eta,2)));
				}
			}
		}
	} catch(LineSearch::NullDirectionException&) {

	}

	// update of x
	p=0;
	for (int i=0; i<n; i++) {
		if (!I[i]) {
			x[i] = hx[p];
			p++;
		}
	}
	cout << " [conj_grad] new x= " << x << endl;
	return x;
}

} // end namespace
