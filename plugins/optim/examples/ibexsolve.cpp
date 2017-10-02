//============================================================================
//                                  I B E X
//
//                               ************
//                                 IbexSolve
//                               ************
//
// Author      : Gilles Chabert
// Copyright   : IMT Atlantique (France)
// License     : See the LICENSE file
// Last Update : Oct 01, 2017
//============================================================================

#include "ibex.h"
#include "args.hxx"

#include <sstream>

#ifndef _IBEX_WITH_OPTIM_
#error "You need the plugin Optim to run this example."
#endif

using namespace std;
using namespace ibex;

int main(int argc, char** argv) {

	stringstream _random_seed, _eps_x_min, _eps_x_max;
	_random_seed << "Random seed (useful for reproducibility). Default value is " << DefaultSolver::default_random_seed << ".";
	_eps_x_min << "Minimal width of output boxes. This is a criterion to _stop_ bisection: a "
			"non-validated box will not be larger than 'eps-min'. Default value is 1e" << round(::log10(DefaultSolver::default_eps_x_min)) << ".";
	_eps_x_max << "Maximal width of output boxes. This is a criterion to _force_ bisection: a "
			"validated box will not be larger than 'eps-max' (unless there is no equality and it is fully inside inequalities)."
			" Default value is +oo (none)";

	args::ArgumentParser parser("********* IbexSolve (defaultsolver) *********.", "Solve a Minibex file.");
	args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
	args::ValueFlag<double> eps_x_min(parser, "float", _eps_x_min.str(), {'e', "eps-min"});
	args::ValueFlag<double> eps_x_max(parser, "float", _eps_x_max.str(), {'E', "eps-max"});
	args::ValueFlag<double> timeout(parser, "float", "Timeout (time in seconds). Default value is +oo (none).", {'t', "timeout"});
	args::ValueFlag<string> input_file(parser, "filename", "Manifold input file. The file contains a "
			"(intermediate) description of the manifold with boxes in the MNF (binary) format.", {'i',"input"});
	args::ValueFlag<string> output_file(parser, "filename", "Manifold output file. The file will contain the "
			"description of the manifold with boxes in the MNF (binary) format.", {'o',"output"});
	args::Flag format(parser, "format", "Show the output text format", {"format"});
	args::Flag txt(parser, "txt", "Write the output manifold in a easy-to-parse text file. See --format", {"txt"});
	args::Flag mma(parser, "mma", "Write the output manifold as a Mathematica list.", {"mma"});
	args::Flag trace(parser, "trace", "Activate trace. \"Solutions\" (output boxes) are displayed as and when they are found.", {"trace"});
	args::ValueFlag<string> boundary_test_arg(parser, "true|full-rank|half-ball|false", "Boundary test strength. Possible values are:\n"
			"\t\t* true:\talways satisfied. Set by default for under constrained problems (0<m<n).\n"
			"\t\t* full-rank:\tthe gradients of all constraints (equalities and potentially activated inequalities) must be linearly independent.\n"
			"\t\t* half-ball:\t(**not implemented yet**) the intersection of the box and the solution set is homeomorphic to a half-ball of R^n\n"
	        "\t\t* false: never satisfied. Set by default if m=0 or m=n (inequalities only/square systems)",
			{"boundary"});
	args::Flag sols(parser, "sols", "Display the \"solutions\" (output boxes) on the standard output.", {'s',"sols"});
	args::ValueFlag<double> random_seed(parser, "float", _random_seed.str(), {"random-seed"});
	args::Flag quiet(parser, "quiet", "Print no report on the standard output.",{'q',"quiet"});

	args::Positional<std::string> filename(parser, "filename", "The name of the MINIBEX file.");

	try
	{
		parser.ParseCLI(argc, argv);
	}
	catch (args::Help&)
	{
		std::cout << parser;
		return 0;
	}
	catch (args::ParseError& e)
	{
		std::cerr << e.what() << std::endl;
		std::cerr << parser;
		return 1;
	}
	catch (args::ValidationError& e)
	{
		std::cerr << e.what() << std::endl;
		std::cerr << parser;
		return 1;
	}

	if (format) {
		cout << Manifold::format() << endl;
		exit(0);
	}

	if (filename.Get()=="") {
		ibex_error("no input file (try ibexsolve --help)");
		exit(1);
	}

	try {

		// Load a system of equations
		System sys(filename.Get().c_str());

		string output_manifold_file; // manifold output file
		bool overwitten=false;       // is it overwritten?
		string manifold_copy;

		if (!quiet) {
			cout << endl << "***************************** setup *****************************" << endl;
			cout << "  file loaded:\t\t" << filename.Get() << endl;

			if (eps_x_min)
				cout << "  eps-x:\t\t" << eps_x_min.Get() << "\t(precision on variables domain)" << endl;

			// Fix the random seed for reproducibility.
			if (random_seed)
				cout << "  random seed:\t\t" << random_seed.Get() << endl;

		}

		if (output_file) {
			output_manifold_file = output_file.Get();
		} else {
			// got from stackoverflow.com:
			string::size_type const p(filename.Get().find_last_of('.'));
			// filename without extension
			string filename_no_ext=filename.Get().substr(0, p);
			stringstream ss;
			ss << filename_no_ext << ".mnf";
			output_manifold_file=ss.str();

			ifstream file;
			file.open(output_manifold_file.c_str(), ios::in); // to check if it exists

			if (file.is_open()) {
				overwitten = true;
				stringstream ss;
				ss << filename.Get().c_str() << "~";
				manifold_copy=ss.str();
				// got from stackoverflow.com:
				ofstream dest(manifold_copy, ios::binary);

			    istreambuf_iterator<char> begin_source(file);
			    istreambuf_iterator<char> end_source;
			    ostreambuf_iterator<char> begin_dest(dest);
			    copy(begin_source, end_source, begin_dest);
			}
			file.close();
		}

		if (!quiet) {
			cout << "  output file:\t\t" << output_manifold_file << "\n";
			if (mma)
				cout << "  output format:\tMMA" << endl;
			else if (txt)
				cout << "  output format:\tTXT" << endl;
		}

		// Build the default solver
		DefaultSolver s(sys,
				eps_x_min ? eps_x_min.Get() : DefaultSolver::default_eps_x_min,
				eps_x_max ? eps_x_max.Get() : DefaultSolver::default_eps_x_max,
				random_seed? random_seed.Get() : DefaultSolver::default_random_seed);

		if (boundary_test_arg) {

			if (boundary_test_arg.Get()=="true")
				s.boundary_test = Solver::ALL_TRUE;
			else if (boundary_test_arg.Get()=="full-rank")
				s.boundary_test = Solver::FULL_RANK;
			else if (boundary_test_arg.Get()=="half-ball")
				s.boundary_test = Solver::HALF_BALL;
			else if (boundary_test_arg.Get()=="false")
				s.boundary_test = Solver::ALL_FALSE;
			else {
				cerr << "\nError: \"" << boundary_test_arg.Get() << "\" is not a valid option (try --help)\n";
				exit(0);
			}

			if (!quiet)
				cout << "  boundary test:\t\t" << boundary_test_arg.Get() << endl;
		}

		// This option limits the search time
		if (timeout) {
			if (!quiet)
				cout << "  timeout:\t\t" << timeout.Get() << "s" << endl;
			s.time_limit=timeout.Get();
		}

		// This option prints each better feasible point when it is found
		if (trace) {
			if (!quiet)
				cout << "  trace:\t\tON" << endl;
			s.trace=trace.Get();
		}

		if (!quiet) {
			cout << "*****************************************************************" << endl << endl;
		}

		if (!quiet)
			cout << "running............" << endl << endl;

		// Get the solutions
		if (input_file)
			s.solve(input_file.Get().c_str());
		else
			s.solve(sys.box);

		if (trace) cout << endl;

		if (!quiet) s.report();

		if (sols) cout << s.get_manifold() << endl;

		if (txt || mma)
			s.get_manifold().write_txt(output_manifold_file.c_str(), mma);
		else
			s.get_manifold().write(output_manifold_file.c_str());

		if (!quiet) {
			cout << " results written in " << output_manifold_file << "\n";
			if (overwitten)
				cout << " (old file saved in " << manifold_copy << ")\n";
		}
		//		if (!quiet && !sols) {
//			cout << " (note: use --sols to display solutions)" << endl;
//		}

	}
	catch(ibex::UnknownFileException& e) {
		cerr << "Error: cannot read file '" << filename.Get() << "'" << endl;
	}
	catch(ibex::SyntaxError& e) {
		cout << e << endl;
	}
}