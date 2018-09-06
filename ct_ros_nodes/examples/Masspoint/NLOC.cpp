#include <ct/optcon/optcon.h>

#include "Masspoint.h"
#include "TermFrictionWork.h"
#include "../exampleDir.h"

#include <matlabCppInterface/MatFile.hpp>


using namespace ct::core;
using namespace ct::optcon;


int main(int argc, char** argv)
{
    /*get the state and control input dimension of the oscillator. Since we're dealing with a simple oscillator,
	 the state and control dimensions will be state_dim = 2, and control_dim = 1. */
    const size_t state_dim = Masspoint<double>::STATE_DIM;
    const size_t control_dim = Masspoint<double>::CONTROL_DIM;


    /* STEP 1: set up the Nonlinear Optimal Control Problem
	 * First of all, we need to create instances of the system dynamics, the linearized system and the cost function. */

    double mass = 1.0;  // [kg] a wild guess for the weight of a hand-held tool
    double mu = 0.1;    // [-] a wild guess for a dynamic friction coefficient
    std::shared_ptr<ct::core::ControlledSystem<state_dim, control_dim>> masspointDynamics(
        new Masspoint<double>(mass, mu));


    /* STEP 1-B: Although the first order derivatives of the oscillator are easy to derive, let's illustrate the use of the System Linearizer,
	 * which performs numerical differentiation by the finite-difference method. The system linearizer simply takes the
	 * the system dynamics as argument. Alternatively, you could implement your own first-order derivatives by overloading the class LinearSystem.h */
    std::shared_ptr<ct::core::SystemLinearizer<state_dim, control_dim>> adLinearizer(
        new ct::core::SystemLinearizer<state_dim, control_dim>(masspointDynamics));


    /* STEP 1-C: create a cost function. We have pre-specified the cost-function weights for this problem in "nlocCost.info".
	 * Here, we show how to create terms for intermediate and final cost and how to automatically load them from the configuration file.
	 * The verbose option allows to print information about the loaded terms on the terminal. */
    std::shared_ptr<ct::optcon::TermQuadratic<state_dim, control_dim>> intermediateCost(
        new ct::optcon::TermQuadratic<state_dim, control_dim>());
    std::shared_ptr<ct::optcon::TermQuadratic<state_dim, control_dim>> finalCost(
        new ct::optcon::TermQuadratic<state_dim, control_dim>());
    bool verbose = false;
    intermediateCost->loadConfigFile(ct::ros::exampleDir + "/Masspoint/nlocCost.info", "intermediateCost", verbose);
    finalCost->loadConfigFile(ct::ros::exampleDir + "/Masspoint/nlocCost.info", "finalCost", verbose);

    double Q_friction_work;
    ct::core::loadScalar(ct::ros::exampleDir + "/Masspoint/nlocCost.info", "Q_friction_work", Q_friction_work);

    std::shared_ptr<ct::TermFrictionWork> termFrictionWork(new ct::TermFrictionWork(Q_friction_work, mass, mu));

    // Since we are using quadratic cost function terms in this example, the first and second order derivatives are immediately known and we
    // define the cost function to be an "Analytical Cost Function". Let's create the corresponding object and add the previously loaded
    // intermediate and final term.
    std::shared_ptr<CostFunctionQuadratic<state_dim, control_dim>> costFunction(
        new CostFunctionAnalytical<state_dim, control_dim>());
    costFunction->addIntermediateTerm(intermediateCost);
    costFunction->addIntermediateTerm(termFrictionWork);
    costFunction->addFinalTerm(finalCost);


    /* STEP 1-D: initialization with initial state and desired time horizon */

    StateVector<state_dim> x0;
    x0.setZero();

    ct::core::Time timeHorizon = 1.0;  // and a final time horizon in [sec]


    // STEP 1-E: create and initialize an "optimal control problem"
    ContinuousOptConProblem<state_dim, control_dim> optConProblem(
        timeHorizon, x0, masspointDynamics, costFunction, adLinearizer);


    /* STEP 2: set up a nonlinear optimal control solver. */

    /* STEP 2-A: Create the settings.
	 * the type of solver, and most parameters, like number of shooting intervals, etc.,
	 * can be chosen using the following settings struct. Let's use, the iterative
	 * linear quadratic regulator, iLQR, for this example. In the following, we
	 * modify only a few settings, for more detail, check out the NLOptConSettings class. */
    NLOptConSettings ilqr_settings;
    ilqr_settings.dt = 0.01;  // the control discretization in [sec]
    ilqr_settings.integrator = ct::core::IntegrationType::EULERCT;
    ilqr_settings.discretization = NLOptConSettings::APPROXIMATION::FORWARD_EULER;
    ilqr_settings.max_iterations = 10;
    ilqr_settings.nThreads = 1;  // use multi-threading
    ilqr_settings.nlocp_algorithm = NLOptConSettings::NLOCP_ALGORITHM::ILQR;
    ilqr_settings.lqocp_solver = NLOptConSettings::LQOCP_SOLVER::GNRICCATI_SOLVER;  // custom Riccati solver
    ilqr_settings.printSummary = true;


    /* STEP 2-B: provide an initial guess */
    // calculate the number of time steps K
    size_t K = ilqr_settings.computeK(timeHorizon);

    /* design trivial initial controller for iLQR. Note that in this simple example,
	 * we can simply use zero feedforward with zero feedback gains around the initial position.
	 * In more complex examples, a more elaborate initial guess may be required.*/
    FeedbackArray<state_dim, control_dim> u0_fb(K, FeedbackMatrix<state_dim, control_dim>::Zero());
    ControlVectorArray<control_dim> u0_ff(K, ControlVector<control_dim>::Zero());
    StateVectorArray<state_dim> x_ref_init(K + 1, x0);
    NLOptConSolver<state_dim, control_dim>::Policy_t initController(x_ref_init, u0_ff, u0_fb, ilqr_settings.dt);


    // STEP 2-C: create an NLOptConSolver instance
    NLOptConSolver<state_dim, control_dim> iLQR(optConProblem, ilqr_settings);

    // set the initial guess
    iLQR.setInitialGuess(initController);


    // STEP 3: solve the optimal control problem
    iLQR.solve();


    // STEP 4: retrieve the solution
    ct::core::StateFeedbackController<state_dim, control_dim> solution = iLQR.getSolution();


    //    std::cout << "Printing the solution trajectory. " << std::endl;
    //    for (size_t i = 0; i < solution.x_ref().size(); i++)
    //        std::cout << "x: \t" << solution.x_ref()[i].transpose() << "\t Forces: \t" << solution.uff()[i].transpose()
    //                  << std::endl;

    // logging to matlab
    matlab::MatFile matFile;
    matFile.open(ct::ros::exampleDir + "/Masspoint/masspointLog.mat");
    matFile.put("x", solution.x_ref().toImplementation());
    matFile.put("u", solution.uff().toImplementation());
    matFile.put("t", ct::core::tpl::TimeArray<double>(solution.time()).toEigenTrajectory());
    matFile.close();
}