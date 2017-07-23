//                                  I B E X                                   
// File        : ibex_Optimizer.cpp
// Author      : Gilles Chabert, Bertrand Neveu
// Copyright   : Ecole des Mines de Nantes (France)
// License     : See the LICENSE file
// Created     : May 14, 2012
// Last Update : December 24, 2012
//============================================================================

#include "ibex_Optimizer.h"
#include "ibex_Timer.h"
#include "ibex_Function.h"
#include "ibex_NoBisectableVariableException.h"
#include "ibex_OptimData.h"

#include <float.h>
#include <stdlib.h>

using namespace std;

namespace ibex {

const double Optimizer::default_eps_x = 0;
const double Optimizer::default_rel_eps_f = 1e-03;
const double Optimizer::default_abs_eps_f = 1e-07;
const double Optimizer::default_random_seed = 1.0;

void Optimizer::write_ext_box(const IntervalVector& box, IntervalVector& ext_box) {
	int i2=0;
	for (int i=0; i<n; i++,i2++) {
		if (i2==goal_var) i2++; // skip goal variable
		ext_box[i2]=box[i];
	}
}

void Optimizer::read_ext_box(const IntervalVector& ext_box, IntervalVector& box) {
	int i2=0;
	for (int i=0; i<n; i++,i2++) {
		if (i2==goal_var) i2++; // skip goal variable
		box[i]=ext_box[i2];
	}
}

Optimizer::Optimizer(const System& user_sys, Ctc& ctc, Bsc& bsc, LoupFinder& finder,
		int goal_var, double eps_x, double rel_eps_f, double abs_eps_f,
		bool rigor,  int critpr,CellCostFunc::criterion crit2) :
                				user_sys(user_sys),
                				n(user_sys.nb_var), goal_var(goal_var), has_equality(false /* by default*/),
                				ctc(ctc), bsc(bsc), loup_finder(finder), loup_correc(user_sys,trace),
                				buffer(*new CellCostVarLB(n), *CellCostFunc::get_cost(crit2, n), critpr),  // first buffer with LB, second buffer with ct (default UB))
                				eps_x(eps_x), rel_eps_f(rel_eps_f), abs_eps_f(abs_eps_f),
                				trace(false), timeout(-1),
                				rigor(rigor), status(SUCCESS),
                				//kkt(normalized_user_sys),
								uplo(NEG_INFINITY), uplo_of_epsboxes(POS_INFINITY), loup(POS_INFINITY), pseudo_loup(POS_INFINITY),
                				loup_point(n), loup_box(n), initial_loup(POS_INFINITY), loup_changed(false), nb_cells(0), time(0) {

	// ==== check if the system contains equalities ====
	for (int i=0; i<user_sys.ctrs.size(); i++) {
		if (user_sys.ctrs[i].op==EQ) {
			(bool&) has_equality = true;
			break;
		}
	}

	if (trace) cout.precision(12);
}

Optimizer::~Optimizer() {
	buffer.flush();
	delete &buffer.cost1();
	delete &buffer.cost2();
}

// compute the value ymax (decreasing the loup with the precision)
// the heap and the current box are contracted with y <= ymax
double Optimizer::compute_ymax() {
	double ymax = loup - rel_eps_f*fabs(loup);
	if (loup - abs_eps_f < ymax)
		ymax = loup - abs_eps_f;
	return ymax;
}

bool Optimizer::update_loup(const IntervalVector& box) {

	bool loup_change=false;

	try {
		pair<Vector,double> p=loup_finder.find(box,loup_point,pseudo_loup);
		loup_point = p.first;
		pseudo_loup = p.second;

		if (trace)
			cout << setprecision (12) << " loup update=" << pseudo_loup << " loup point=" << loup_point << endl;

		if (rigor && has_equality) {
			// a loup point will not be safe (pseudo loup is not the real loup)
			try {
				pair<IntervalVector,double> p_corr=loup_correc.find(loup,loup_point,pseudo_loup);
				loup_box = p_corr.first;
				loup = p_corr.second;

				if (trace)
					cout << setprecision (12) << " *real* loup update " << loup  << " loup box: " << loup_box << endl;

				loup_change = true;

			} catch(LoupCorrection::NotFound&) { }

		} else {
			// the loup point is safe: the pseudo loup is the real loup.
			loup = pseudo_loup;
			loup_change = true;
		}

	} catch(LoupFinder::NotFound&) { }

	return loup_change;
}

//bool Optimizer::update_entailed_ctr(const IntervalVector& box) {
//	for (int j=0; j<m; j++) {
//		if (entailed->normalized(j)) {
//			continue;
//		}
//		Interval y=sys.ctrs[j].f.eval(box);
//		if (y.lb()>0) return false;
//		else if (y.ub()<=0) {
//			entailed->set_normalized_entailed(j);
//		}
//	}
//	return true;
//}

void Optimizer::update_uplo() {
	double new_uplo=POS_INFINITY;

	if (! buffer.empty()) {
		new_uplo= buffer.minimum();
		if (new_uplo > loup) {
			cout << " loup = " << loup << " new_uplo=" << new_uplo << endl;
			ibex_error("optimizer: new_uplo>loup (please report bug)");
		}
		if (new_uplo < uplo) {
			cout << "uplo= " << uplo << " new_uplo=" << new_uplo << endl;
			ibex_error("optimizer: new_uplo<uplo (please report bug)");
		}

		// uplo <- max(uplo, min(new_uplo, uplo_of_epsboxes))
		if (new_uplo < uplo_of_epsboxes) {
			if (new_uplo > uplo) {
				uplo = new_uplo;
			}
		}
		else uplo= uplo_of_epsboxes;
	}
	else if (buffer.empty() && loup != POS_INFINITY) {
		// empty buffer : new uplo is set to ymax (loup - precision) if a loup has been found
		new_uplo=compute_ymax(); // not new_uplo=loup, because constraint y <= ymax was enforced
		//    cout << " new uplo buffer empty " << new_uplo << " uplo " << uplo << endl;

		double m = minimum(new_uplo, uplo_of_epsboxes);
		if (uplo < m) uplo = m; // warning: hides the field "m" of the class
		// note: we always have uplo <= uplo_of_epsboxes but we may have uplo > new_uplo, because
		// ymax is strictly lower than the loup.
	}

}

void Optimizer::update_uplo_of_epsboxes(double ymin) {

	// the current box cannot be bisected.  ymin is a lower bound of the objective on this box
	// uplo of epsboxes can only go down, but not under uplo : it is an upperbound for uplo,
	//that indicates a lowerbound for the objective in all the small boxes
	// found by the precision criterion
	assert (uplo_of_epsboxes >= uplo);
	assert(ymin >= uplo);
	if (uplo_of_epsboxes > ymin) {
		uplo_of_epsboxes = ymin;
		if (trace) {
			cout << "uplo_of_epsboxes:" << setprecision(12) <<  uplo_of_epsboxes << " uplo " << uplo << endl;
		}
	}
}

void Optimizer::handle_cell(Cell& c, const IntervalVector& init_box ){

	contract_and_bound(c, init_box);

	if (c.box.is_empty()) {
		delete &c;
	} else {
		// we know cost1() does not require OptimData
		buffer.cost2().set_optim_data(c,user_sys);

		// the cell is put into the 2 heaps
		buffer.push(&c);

		nb_cells++;
	}
}

void Optimizer::contract_and_bound(Cell& c, const IntervalVector& init_box) {

	/*======================== contract y with y<=loup ========================*/
	Interval& y=c.box[goal_var];

	double ymax;
	if (loup==POS_INFINITY) ymax = POS_INFINITY;
	// ymax is slightly increased to favour subboxes of the loup
	// TODO: useful with double heap??
	else ymax = compute_ymax()+1.e-15;

	y &= Interval(NEG_INFINITY,ymax);

	if (y.is_empty()) {
		c.box.set_empty();
		return;
	}

	/*================ contract x with f(x)=y and g(x)<=0 ================*/
	//cout << " [contract]  x before=" << c.box << endl;
	//cout << " [contract]  y before=" << y << endl;

	ctc.contract(c.box);

	if (c.box.is_empty()) return;

	//cout << " [contract]  x after=" << c.box << endl;
	//cout << " [contract]  y after=" << y << endl;
	/*====================================================================*/

	/*========================= update loup =============================*/

	IntervalVector tmp_box(n);
	read_ext_box(c.box,tmp_box);

//	entailed = &c.get<EntailedCtr>();
//	if (!update_entailed_ctr(tmp_box)) {
//		c.box.set_empty();
//		return;
//	}

	bool loup_ch=update_loup(tmp_box);

	// update of the upper bound of y in case of a new loup found
	if (loup_ch) y &= Interval(NEG_INFINITY,compute_ymax());

	//TODO: should we propagate constraints again?

	loup_changed |= loup_ch;

	if (y.is_empty()) { // fix issue #44
		c.box.set_empty();
		return;
	}

	/*====================================================================*/
	// Note: there are three different cases of "epsilon" box,
	// - NoBisectableVariableException raised by the bisector (---> see optimize(...)) which
	//   is independent from the optimizer
	// - the width of the box is less than the precision given to the optimizer ("prec" for the original variables
	//   and "goal_abs_prec" for the goal variable)
	// - the extended box has no bisectable domains (if prec=0 or <1 ulp)
	if ((tmp_box.max_diam()<=eps_x && y.diam() <=abs_eps_f) || !c.box.is_bisectable()) {
		update_uplo_of_epsboxes(y.lb());
		c.box.set_empty();
		return;
	}

	// ** important: ** must be done after upper-bounding
	//kkt.contract(tmp_box);

	if (tmp_box.is_empty()) {
		c.box.set_empty();
	} else {
		// the current extended box in the cell is updated
		write_ext_box(tmp_box,c.box);
	}
}

Optimizer::Status Optimizer::optimize(const IntervalVector& init_box, double obj_init_bound) {

	RNG::srand(random_seed);

	loup=obj_init_bound;
	pseudo_loup=obj_init_bound;

	// Just to initialize the "loup" for the buffer
	// TODO: replace with a set_loup function
	buffer.contract(loup);

	uplo=NEG_INFINITY;
	uplo_of_epsboxes=POS_INFINITY;

	nb_cells=0;

	buffer.flush();

	Cell* root=new Cell(IntervalVector(n+1));

	write_ext_box(init_box,root->box);

	// add data required by the bisector
	bsc.add_backtrackable(*root);

	// add data "pu" and "pf" (if required)
	buffer.cost2().add_backtrackable(*root);

	// add data required by optimizer + KKT contractor
//	root->add<EntailedCtr>();
//	//root->add<Multipliers>();
//	entailed=&root->get<EntailedCtr>();
//	entailed->init_root(user_sys,sys);

	loup_changed=false;
	initial_loup=obj_init_bound;

	// TODO: no loup-point if handle_cell contracts everything
	loup_point=init_box.mid();
	time=0;
	Timer::start();
	handle_cell(*root,init_box);

	update_uplo();

	try {
		while (!buffer.empty()) {
		  //			if (trace >= 2) cout << " buffer " << buffer << endl;
		  if (trace >= 2) buffer.print(cout);
			//		  cout << "buffer size "  << buffer.size() << " " << buffer2.size() << endl;

			loup_changed=false;

			Cell *c = buffer.top();

			try {
				pair<IntervalVector,IntervalVector> boxes=bsc.bisect(*c);

				pair<Cell*,Cell*> new_cells=c->bisect(boxes.first,boxes.second);

				buffer.pop();
				delete c; // deletes the cell.

				handle_cell(*new_cells.first, init_box);
				handle_cell(*new_cells.second, init_box);

				if (uplo_of_epsboxes == NEG_INFINITY) {
					cout << " possible infinite minimum " << endl;
					break;
				}
				if (loup_changed) {
					// In case of a new upper bound (loup_changed == true), all the boxes
					// with a lower bound greater than (loup - goal_prec) are removed and deleted.
					// Note: if contraction was before bisection, we could have the problem
					// that the current cell is removed by contractHeap. See comments in
					// older version of the code (before revision 284).

					double ymax=compute_ymax();

					buffer.contract(ymax);
					//cout << " now buffer is contracted and min=" << buffer.minimum() << endl;

					// TODO: check if happens. What is the return code in this case?
					if (ymax <= NEG_INFINITY) {
						if (trace) cout << " infinite value for the minimum " << endl;
						break;
					}
					if (trace) cout << setprecision(12) << "ymax=" << ymax << " uplo= " <<  uplo<< endl;
				}
				update_uplo();
				time_limit_check(); // TODO: not reentrant

			}
			catch (NoBisectableVariableException& ) {
				update_uplo_of_epsboxes((c->box)[goal_var].lb());
				buffer.pop();
				delete c; // deletes the cell.
				//if (trace>=1) cout << "epsilon-box found: uplo cannot exceed " << uplo_of_epsboxes << endl;
				update_uplo(); // the heap has changed -> recalculate the uplo (eg: if not in best-first search)

			}
		}
	}
	catch (TimeOutException& ) {
		status = TIME_OUT;
		return status;
	}

	Timer::stop();
	time+= Timer::VIRTUAL_TIMELAPSE();

	if (uplo_of_epsboxes == POS_INFINITY && (loup==POS_INFINITY || (loup==initial_loup && abs_eps_f==0 && rel_eps_f==0)))
		status=INFEASIBLE;
	else if (loup==initial_loup)
		status=NO_FEASIBLE_FOUND;
	else if (uplo_of_epsboxes == NEG_INFINITY)
		status=UNBOUNDED_OBJ;
	else if (get_obj_rel_prec()>rel_eps_f && get_obj_abs_prec()>abs_eps_f)
		status=UNREACHED_PREC;
	else
		status=SUCCESS;

	return status;
}

void Optimizer::report(bool verbose) {

	if (!verbose) {
		cout << get_status() << endl;
		cout << get_uplo() << ' ' << get_loup() << endl;
		for (int i=0; i<n; i++) {
			if (i>0) cout << ' ';
			cout << get_loup_point()[i];
		}
		cout << endl << get_time() << " " << get_nb_cells() << endl;
		return;
	}

	switch(status) {
	case SUCCESS: cout << "\033[32m" << " optimization successful!" << endl;
	break;
	case INFEASIBLE: cout << "\033[31m" << " infeasible problem" << endl;
	break;
	case NO_FEASIBLE_FOUND: cout << "\033[31m" << " no feasible point found (the problem may be infesible)" << endl;
	break;
	case UNBOUNDED_OBJ: cout << "\033[31m" << " possibly unbounded objective (f*=-oo)" << endl;
	break;
	case TIME_OUT: cout << "\033[31m" << " time limit " << timeout << "s. reached " << endl;
	break;
	case UNREACHED_PREC: cout << "\033[31m" << " unreached precision" << endl;
	}

	cout << "\033[0m" << endl;

	// No solution found and optimization stopped with empty buffer  before the required precision is reached => means infeasible problem
	if (buffer.empty() && uplo_of_epsboxes == POS_INFINITY && (loup==POS_INFINITY || (loup==initial_loup && abs_eps_f==0 && rel_eps_f==0))) {
		cout << " infeasible problem " << endl;
	} else {
		cout << " best bound in: [" << uplo << "," << loup << "]" << endl;

		double rel_prec=get_obj_rel_prec();
		double abs_prec=get_obj_abs_prec();

		cout << " relative precision obtained on objective function: " << rel_prec << " " <<
				(rel_prec <= rel_eps_f? " [passed]" : " [failed]") << endl;

		cout << " absolute precision obtained on objective function: " << abs_prec << " " <<
				(abs_prec <= abs_eps_f? " [passed]" : " [failed]") << endl;

		if (loup==initial_loup)
			cout << " no feasible point found " << endl;
		else
			cout << " best feasible point: " << loup_point << endl;

	}
	cout << " cpu time used: " << time << "s." << endl;
	cout << " number of cells: " << nb_cells << endl;
}

void Optimizer::time_limit_check () {
	if (timeout<=0) return;
	Timer::stop();
	time += Timer::VIRTUAL_TIMELAPSE();
	if (time >=timeout ) throw TimeOutException();
	Timer::start();
}

} // end namespace ibex
