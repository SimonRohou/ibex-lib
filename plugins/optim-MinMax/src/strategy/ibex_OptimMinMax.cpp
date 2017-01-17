//============================================================================
//                                  I B E X
// File        : ibex_OptimMinMax.cpp
// Author      : Dominique Monnet, Jordan Ninin
// License     : See the LICENSE file
// Created     : Oct 1, 2016
//============================================================================

#include "ibex_OptimMinMax.h"
#include "ibex_LargestFirst.h"
#include "ibex_Timer.h"
#include <stdio.h>
#include "ibex_DataMinMax.h"
#include "ibex_NoBisectableVariableException.h"

using namespace std;

namespace ibex {

const int OptimMinMax::default_iter = 10;
const int OptimMinMax::default_list_rate = 0;
const double OptimMinMax::default_min_perc_coef = 0;
const int OptimMinMax::default_list_elem_absolute_max = 1000;

OptimMinMax::OptimMinMax(NormalizedSystem& x_sys,NormalizedSystem& xy_sys, Ctc& x_ctc,Ctc& xy_ctc,double prec_x,double prec_y,double goal_rel_prec):
                Optim(x_sys.nb_var, new CellDoubleHeap(*new CellCostFmaxlb(), *new CellCostFmaxub()),
				prec_x, goal_rel_prec, goal_rel_prec, 1), // attention meme precision en relatif et en absolue
	x_box_init(x_sys.box), y_box_init(xy_sys.box.subvector(x_sys.nb_var, xy_sys.nb_var-1)), trace_freq(10000),
        x_ctc(x_ctc),x_sys(x_sys),lsolve(xy_sys,xy_ctc),
	bsc(new LargestFirst()),
        prec_y(prec_y),
        iter(default_iter),
        list_rate(default_list_rate),
        min_perc_coef(default_min_perc_coef),
        list_elem_absolute_max(default_list_elem_absolute_max){
};

OptimMinMax::~OptimMinMax() {
	buffer->flush();
	delete &(buffer->cost1());
	delete &(buffer->cost2());
	delete buffer;
	delete bsc;
}

Optim::Status OptimMinMax::optimize() {
	return optimize(x_sys.box,POS_INFINITY);
}


Optim::Status OptimMinMax::optimize(const IntervalVector& x_box_ini1, double obj_init_bound) {

	if (trace) lsolve.trace =trace;
	// we estimate that one iteration is at most 1% of the total time
	lsolve.timeout=timeout/100;

	loup=obj_init_bound;

	uplo=NEG_INFINITY;
	uplo_of_epsboxes=POS_INFINITY;

	x_box_init = x_box_ini1;

	//****** initialization of the first Cell********
	Cell * root = new Cell(x_box_init);
	bsc->add_backtrackable(*root);
	lsolve.add_backtrackable(*root,y_box_init);
	buffer->cost1().add_backtrackable(*root);
	buffer->cost2().add_backtrackable(*root);


	//****** x_heap initialization ********
	nb_cells=0;
	buffer->flush();

	// *** initialisation Algo ***
	loup_changed=false;
	initial_loup=obj_init_bound;
	loup_point=x_box_init.mid();
	time=0;
	Timer::reset_time();
	Timer::start();

	handle_cell(root);

	update_uplo();

	try {
		while (!buffer->empty()) {
			//			if (trace >= 2) cout << " buffer " << buffer << endl;
			if (trace >= 2) buffer->print(cout);


			loup_changed=false;

			Cell *c = buffer->pop();

			try {
				pair<Cell*,Cell*> new_cells=bsc->bisect_cell(*c);
				delete c; // deletes the cell.

				handle_cell(new_cells.first);

				handle_cell(new_cells.second);

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

					buffer->contract(ymax);
					//cout << " now buffer is contracted and min=" << buffer.minimum() << endl;


					if (ymax <= NEG_INFINITY) {
						if (trace) cout << " infinite value for the minimum " << endl;
						break;
					}
					if (trace) cout <<  "iter="<< nb_cells <<",  size_heap="<< buffer->size()<< ",  ymax=" << ymax << ",  uplo= " <<  uplo<< endl;
				}
				update_uplo();
				Timer::check(timeout);

			}
			catch (NoBisectableVariableException& ) {
				bool res=handle_cell(c);
				if (res) update_uplo_of_epsboxes(c->get<DataMinMax>().fmax.lb());
				//if (trace>=1) cout << "epsilon-box found: uplo cannot exceed " << uplo_of_epsboxes << endl;
				update_uplo(); // the heap has changed -> recalculate the uplo

			}
		}
	}
	catch (TimeOutException& ) {
		Timer::stop();
		time = Timer::get_time();
		return TIME_OUT;
	}

	Timer::stop();
	time = Timer::get_time();

	if (uplo_of_epsboxes == POS_INFINITY && (loup==POS_INFINITY || (loup==initial_loup && goal_abs_prec==0 && goal_rel_prec==0)))
		return INFEASIBLE;
	else if (loup==initial_loup)
		return NO_FEASIBLE_FOUND;
	else if (uplo_of_epsboxes == NEG_INFINITY)
		return UNBOUNDED_OBJ;
	else
		return SUCCESS;
}





bool  OptimMinMax::handle_cell(Cell * x_cell) {

	DataMinMax * data_x = &(x_cell->get<DataMinMax>());

	// update the upper bound with the loup
	data_x->fmax &= Interval(NEG_INFINITY,loup);
	//std::cout <<" debut "<<data_x->fmax <<std::endl;

	if (data_x->fmax.is_empty()) {
		delete x_cell;
		return false;
	}

	//***************** contraction w.r.t constraint on x ****************
	if(check_constraints(x_cell->box)==2)
		data_x->pu=1;
	if (data_x->pu != 1)     {
		x_ctc.contract(x_cell->box);
		if(x_cell->box.is_empty()) {
			//vol_rejected += x_cell->box.volume();
			delete x_cell;
			//cout<<"loup : "<<loup<<" get for point: x = "<<best_sol<<" y = "<<max_y<<" uplo: "<<uplo<< " volume rejected: "<<vol_rejected/init_vol*100<<endl;
			return false;
		}
		/*  TODO faire des options de TRACE
            else if(x_cell->box !=  box_mem) {
                vol_rejected += box_mem.volume()-x_cell->box.volume();
                cout<<"loup : "<<loup<<" get for point: x = "<<best_sol<<" y = "<<max_y<<" uplo: "<<uplo<< " volume rejected: "<<vol_rejected/init_vol*100<<endl;
            }
		 */
	}

	//************ evaluation of f(x,y_heap) *****************
        lsolve.prec_y = compute_min_prec(x_cell->box);
        lsolve.nb_iter = choose_nbiter(false);
        lsolve.list_elem_max = compute_heap_max_size(data_x->y_heap->size());
	// compute
//        cout<<"run light optim"<<endl;
        bool res =lsolve.optimize(x_cell);
//        cout<<"light optim result: "<<data_x->fmax<< endl;
	//std::cout<<" apres res="<<res<<" bound= "<<data_x->fmax <<std::endl;

	if(!res) { // certified that x box does not contains the solution
		// TODO Trace
		//vol_rejected += x_cell->box.volume();
		//x_cell->y_heap.flush();
		//delete x_cell;
		//cout<<"loup : "<<loup<<" get for point: x = "<<best_sol<<" y = "<<max_y<<" uplo: "<<uplo<< " volume rejected: "<<vol_rejected/init_vol*100<<endl;
		return false;
	}

	//************* midpoint evaluation ****************
	IntervalVector  midp = get_feasible_point(x_cell);
	if(!midp.is_empty())    { // we found a feasible point
		Cell *x_copy = new Cell(*x_cell); // copy of the Cell and the y_heap
		x_copy->box=midp; // set the box to the middle of x

                lsolve.nb_iter = choose_nbiter(true);
                lsolve.prec_y = prec_y;
                lsolve.list_elem_max = 0; // no limit on heap size
                bool res1 = lsolve.optimize(x_copy); // eval maxf(midp,heap_copy), go to minimum prec on y to get a thin enclosure
		if (res1) {
			double new_loup = x_copy->get<DataMinMax>().fmax.ub();

			if(new_loup<loup) { // update best current solution
				loup = new_loup;
				loup_changed = true;
				loup_point = (midp.mid());
				data_x->fmax &= Interval(NEG_INFINITY,loup);
				if (trace) cout << "[mid]"  << " loup update " << loup  << " loup point  " << loup_point << endl;
				//max_y = heap_copy.top1()->box;
				//cout<<"loup : "<<loup<<" get for point: x = "<<best_sol<<" y = "<<max_y<<" uplo: "<<uplo<< " volume rejected: "<<vol_rejected/init_vol*100<<endl;
			}
			delete x_copy; // delete copy of the heap, no more use after the computation of max f(midp,heap_copy)
		}
	}


	//***** if x_cell is too small ******************
	if(x_cell->box.max_diam()<prec) {
                lsolve.nb_iter = choose_nbiter(true);   // need to be great enough so the minimum precision on y is reached
                lsolve.prec_y = prec_y;
                lsolve.list_elem_max = 0; // no limit on heap size
		//cout<<"fmax ini: "<<x_cell->fmax<<endl;
                bool res =lsolve.optimize(x_cell); // eval maxf(midp,heap_copy), go to minimum prec on y to get a thin enclosure
		//cout<<"fmax min prec: "<<x_cell->fmax<<endl;

		if(res){
			update_uplo_of_epsboxes(data_x->fmax.lb());
			delete x_cell;
		}

		//cout<<"minprec reached! "<<" box: "<<x_cell->box<<" eval full prec: "<<x_cell->fmax <<endl;
		//cout<<"loup : "<<loup<<" get for point: x = "<<best_sol<<" y = "<<max_y<<" uplo: "<<uplo<< " volume rejected: "<<vol_rejected/init_vol*100<<endl;
		//min_prec_reached = true;
		return false;
	}

	// update optim data of the cell
	buffer->cost2().set_optim_data(*x_cell,x_sys);
	buffer->push(x_cell);
	nb_cells++;

	if (trace && (nb_cells%trace_freq==0)) cout <<  "iter="<< nb_cells <<",  size_heap="<< buffer->size()<< ",  loup=" << loup << ",  uplo= " <<  uplo<< endl;

	//std::cout<<" fin "<<data_x->fmax <<std::endl;
	return true;
}



double OptimMinMax::compute_min_prec( const IntervalVector& x_box) {
    if(min_perc_coef == 0)
        return prec_y;

    double ratio = 0;
    for(int r=0;r<x_box_init.size();r++)
            ratio += (x_box_init[r]).diam()/(x_box[r]).diam();
    return ratio/y_box_init.volume()>prec_y?ratio/(10*y_box_init.volume()):prec_y;
}

int OptimMinMax::choose_nbiter(bool midpoint_eval) {

    if(!midpoint_eval)
            return iter;
    else
            return 0; // go to min prec
}

int OptimMinMax::compute_heap_max_size(int y_heap_size) {
    if(list_rate == 0 || y_heap_size>= list_elem_absolute_max) // no constraint on list growth rate or max size already reached
        return list_elem_absolute_max;
    return  y_heap_size+list_rate;
}

IntervalVector OptimMinMax::get_feasible_point(Cell * elem) {
	Vector mid_x = elem->box.mid(); // get the box (x,mid(y))
	if( (x_sys.nb_ctr >0 ) && (elem->get<DataMinMax>().pu != 1)) { // constraint on xy exist and is not proved to be satisfied
		int res = check_constraints(mid_x);
		if(res==0 || res==1)
			return IntervalVector(1,Interval::EMPTY_SET);
	}
	return mid_x;
}

int OptimMinMax::check_constraints(const IntervalVector& box) {
	int res =2;
	for(int i=0;i<x_sys.nb_ctr;i++) {
		Interval int_res = x_sys.ctrs[i].f.eval(box);
		if(int_res.lb()>=0)
			return 0;
		else if(int_res.ub()>=0)
			res = 1;
	}
	return res;
}


}

