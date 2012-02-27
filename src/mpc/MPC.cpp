#include "errors.hh"
#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_VerboseObjectParameterListHelpers.hpp"
#include "Epetra_Comm.h"
#include "Epetra_MpiComm.h"
#include "MPC.hpp"
#include "State.hpp"
#include "chemistry_state.hh"
#include "chemistry_pk.hh"
#include "Flow_State.hpp"
#include "Darcy_PK.hpp"
//#include "SteadyState_Richards_PK.hpp"
#include "Transient_Richards_PK.hpp"
#include "Transport_State.hpp"
#include "Transport_PK.hpp"
// TODO: We are using depreciated parts of boost::filesystem
#define BOOST_FILESYSTEM_VERSION 2
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"


namespace Amanzi
{

using amanzi::chemistry::Chemistry_State;
using amanzi::chemistry::Chemistry_PK;
using amanzi::chemistry::ChemistryException;


MPC::MPC(Teuchos::ParameterList parameter_list_,
         Teuchos::RCP<Amanzi::AmanziMesh::Mesh> mesh_maps_,
         Epetra_MpiComm* comm_,
         Amanzi::ObservationData& output_observations_):
    parameter_list(parameter_list_),
    mesh_maps(mesh_maps_),
    comm(comm_),
    output_observations(output_observations_) {
  mpc_init();
}


void MPC::mpc_init() {
  // set the line prefix for output
  this->setLinePrefix("Amanzi::MPC         ");
  // make sure that the line prefix is printed
  this->getOStream()->setShowLinePrefix(true);

  // Read the sublist for verbosity settings.
  Teuchos::readVerboseObjectSublist(&parameter_list,this);

  using Teuchos::OSTab;
  Teuchos::EVerbosityLevel verbLevel = this->getVerbLevel();
  Teuchos::RCP<Teuchos::FancyOStream> out = this->getOStream();
  OSTab tab = this->getOSTab(); // This sets the line prefix and adds one tab



  mpc_parameter_list =  parameter_list.sublist("MPC");

  reset_times_.resize(0);
  reset_times_dt_.resize(0);
  
  read_parameter_list();

  // let users selectively disable individual process kernels
  // to allow for testing of the process kernels separately
  transport_enabled =
      (mpc_parameter_list.get<string>("disable Transport_PK","no") == "no");
  chemistry_enabled =
      (mpc_parameter_list.get<string>("disable Chemistry_PK","no") == "no");
  flow_enabled =
      (mpc_parameter_list.get<string>("disable Flow_PK","no") == "no");

  if(out.get() && includesVerbLevel(verbLevel,Teuchos::VERB_LOW,true)) {
    *out << "The following process kernels are enabled: ";

    if (flow_enabled) {
      *out << "Flow ";
    }
    if (transport_enabled) {
      *out << "Transport ";
    }
    if (chemistry_enabled) {
      *out << "Chemistry ";
    }
    *out << std::endl;
  }

  if (transport_enabled || flow_enabled || chemistry_enabled) {
    Teuchos::ParameterList state_parameter_list =
        parameter_list.sublist("State");

    // create the state object
    S = Teuchos::rcp( new State( state_parameter_list, mesh_maps) );
  }

  // create auxilary state objects for the process models
  // chemistry...

  if (chemistry_enabled) {
    try {
      CS = Teuchos::rcp( new Chemistry_State( S ) );

      Teuchos::ParameterList chemistry_parameter_list =
          parameter_list.sublist("Chemistry");

      CPK = Teuchos::rcp( new Chemistry_PK(chemistry_parameter_list, CS) );
    } catch (const ChemistryException& chem_error) {
      std::cout << "MPC: Chemistry_PK constructor returned an error: "
                << std::endl << chem_error.what() << std::endl;
      amanzi_throw(chem_error);
    }
  }

  // transport...
  if (transport_enabled) {
    TS = Teuchos::rcp( new AmanziTransport::Transport_State( *S ) );

    Teuchos::ParameterList transport_parameter_list =
        parameter_list.sublist("Transport");

    TPK = Teuchos::rcp( new AmanziTransport::Transport_PK(transport_parameter_list, TS) );
  }

  // flow...
  if (flow_enabled) {
    FS = Teuchos::rcp( new Flow_State( S ) );

    Teuchos::ParameterList flow_parameter_list =
        parameter_list.sublist("Flow");

    flow_model = mpc_parameter_list.get<string>("Flow model","Darcy");
    if (flow_model == "Darcy") {
      FPK = Teuchos::rcp( new Darcy_PK(flow_parameter_list, FS) );
    } else if (flow_model == "Richards") {
      FPK = Teuchos::rcp( new Transient_Richards_PK(flow_parameter_list, FS) );
    } else {
      cout << "MPC: unknown flow model: " << flow_model << endl;
      throw std::exception();
    }
  }
  // done creating auxilary state objects and  process models

  // create the observations
  if ( parameter_list.isSublist("Observation Data") ) {
    Teuchos::ParameterList observation_plist = parameter_list.sublist("Observation Data"); 
    observations = new Amanzi::Unstructured_observations(observation_plist, output_observations);
  } else {
    observations = NULL;
  }

  // create the visualization object
  if (parameter_list.isSublist("Visualization Data"))  {
    Teuchos::ParameterList vis_parameter_list =
        parameter_list.sublist("Visualization Data");
    visualization = new Amanzi::Vis(vis_parameter_list, comm);
    visualization->create_files(*mesh_maps);
  } else {  // create a dummy vis object
    visualization = new Amanzi::Vis();
  }


  // create the restart object
  if (parameter_list.isSublist("Checkpoint Data")) {
    Teuchos::ParameterList checkpoint_parameter_list =
        parameter_list.sublist("Checkpoint Data");
    restart = new Amanzi::Restart(checkpoint_parameter_list, comm);
  } else {
    restart = new Amanzi::Restart();
  }

  // are we restarting from a file?
  // first assume we're not
  restart_requested = false;

  // then check if indeed we are
  if (mpc_parameter_list.isSublist("Restart from Checkpoint Data File")) {
    restart_requested = true;

    Teuchos::ParameterList& restart_parameter_list =
        mpc_parameter_list.sublist("Restart from Checkpoint Data File");

    restart_from_filename = restart_parameter_list.get<string>("Checkpoint Data File Name");
  }
}

void MPC::read_parameter_list()  {
  end_cycle = mpc_parameter_list.get<int>("End Cycle",-1);
  
  Teuchos::ParameterList& ti_list =  mpc_parameter_list.sublist("Time Integration Mode");
  if (ti_list.isSublist("Initialize To Steady")) {
    ti_mode = INIT_TO_STEADY;
    
    Teuchos::ParameterList& init_to_steady_list = ti_list.sublist("Initialize To Steady");

    T0 = init_to_steady_list.get<double>("Start");
    Tswitch = init_to_steady_list.get<double>("Switch");
    T1 = init_to_steady_list.get<double>("End");

    dTsteady = init_to_steady_list.get<double>("Steady Initial Time Step");
    dTtransient = init_to_steady_list.get<double>("Transient Initial Time Step");

    if (init_to_steady_list.isSublist("Time Period Control")) {
      Teuchos::ParameterList& tpc_list = init_to_steady_list.sublist("Time Period Control");
      
      reset_times_ = tpc_list.get<Teuchos::Array<double> >("Period Start Times");
      reset_times_dt_ = tpc_list.get<Teuchos::Array<double> >("Initial Time Step");

      if (reset_times_.size() != reset_times_dt_.size()) {
	Errors::Message message("You must specify the same number of Reset Times and Initial Time Steps under Time Period Control");
	Exceptions::amanzi_throw(message);
      }    
    }



  } else if ( ti_list.isSublist("Steady")) {
    ti_mode = STEADY;
    
    Teuchos::ParameterList& steady_list = ti_list.sublist("Steady");

    T0 = steady_list.get<double>("Start");
    T1 = steady_list.get<double>("End");
    dTsteady = steady_list.get<double>("Initial Time Step");

    

  } else if ( ti_list.isSublist("Transient") ) {
    ti_mode = TRANSIENT;

    Teuchos::ParameterList& transient_list = ti_list.sublist("Transient");

    T0 = transient_list.get<double>("Start");
    T1 = transient_list.get<double>("End");
    dTtransient =  transient_list.get<double>("Initial Time Step");

    if (transient_list.isSublist("Time Period Control")) {
      Teuchos::ParameterList& tpc_list = transient_list.sublist("Time Period Control");
      
      reset_times_ = tpc_list.get<Teuchos::Array<double> >("Period Start Times");
      reset_times_dt_ = tpc_list.get<Teuchos::Array<double> >("Initial Time Step");

      if (reset_times_.size() != reset_times_dt_.size()) {
	Errors::Message message("You must specify the same number of Reset Times and Initial Time Steps under Time Period Control");
	Exceptions::amanzi_throw(message);	
      }
    }



  } else {
    Errors::Message message("MPC: no valid Time Integration Mode was specified, you must specify exactly one of Initialize To Steady, Steady, or Transient.");
    Exceptions::amanzi_throw(message);    
  }
}


void MPC::cycle_driver () {

  enum time_step_limiter_type { FLOW_LIMITS, TRANSPORT_LIMITS, CHEMISTRY_LIMITS, MPC_LIMITS } ;
  time_step_limiter_type tslimiter;

  using Teuchos::OSTab;
  Teuchos::EVerbosityLevel verbLevel = this->getVerbLevel();
  Teuchos::RCP<Teuchos::FancyOStream> out = this->getOStream();
  OSTab tab = this->getOSTab(); // This sets the line prefix and adds one tab

  if (transport_enabled || flow_enabled || chemistry_enabled) {
    // start at time T=T0;
    S->set_time(T0);
  }

  if (chemistry_enabled) {
    try {
      // these are the vectors that chemistry will populate with
      // the names for the auxillary output vectors and the
      // names of components
      std::vector<string> compnames;

      // total view needs this to be outside the constructor
      CPK->InitializeChemistry();
      CPK->set_chemistry_output_names(&auxnames);
      CPK->set_component_names(&compnames);

      // set the names in the visualization object
      S->set_compnames(compnames);

    } catch (const ChemistryException& chem_error) {
      std::cout << "MPC: Chemistry_PK.InitializeChemistry returned an error "
                << std::endl << chem_error.what() << std::endl;
      Exceptions::amanzi_throw(chem_error);
    }
  }

  // set the iteration counter to zero
  int iter = 0;
  S->set_cycle(iter);

  
  // read the checkpoint file as requested
  if (restart_requested == true) {
    // re-initialize the state object
    restart->read_state( *S, restart_from_filename );
    iter = S->get_cycle();
  }

  // write visualization output as requested
  if (chemistry_enabled) {
    // get the auxillary data from chemistry
    Teuchos::RCP<Epetra_MultiVector> aux = CPK->get_extra_chemistry_output_data();
    // write visualization data for timestep if requested
    S->write_vis(*visualization, &(*aux), auxnames);
  } else {
    S->write_vis(*visualization);
  }

  // write a restart dump if requested (determined in dump_state)
  restart->dump_state(*S);

  if (flow_enabled) {
    if (ti_mode == STEADY || ti_mode == INIT_TO_STEADY ) {
      FPK->init_steady(T0, dTsteady);
    } else if ( ti_mode == TRANSIENT ) {
      FPK->init_transient(T0, dTtransient);
    }
  }


  if (flow_enabled || transport_enabled || chemistry_enabled) {

    // make observations
    if (observations) observations->make_observations(*S);

    // we need to create an EpetraMulitVector that will store the
    // intermediate value for the total component concentration
    total_component_concentration_star =
        Teuchos::rcp(new Epetra_MultiVector(*S->get_total_component_concentration()));

    // then start time stepping 
    while (  (S->get_time() < T1)  &&   ((end_cycle == -1) || (iter <= end_cycle)) ) {

      // determine the time step we are now going to take
      double mpc_dT=1e+99, chemistry_dT=1e+99, transport_dT=1e+99, flow_dT=1e+99, limiter_dT=1e+99;


      if (flow_enabled && flow_model == "Richards") {
	if (ti_mode == INIT_TO_STEADY && S->get_last_time() < Tswitch && S->get_time() >= Tswitch) {
	  if(out.get() && includesVerbLevel(verbLevel,Teuchos::VERB_LOW,true)) {
	    *out << "Steady state computation complete... now running in transient mode." << std::endl;
	  }
	  FPK->init_transient(S->get_time(), dTtransient);	
	}
      }

      if (flow_enabled && flow_model == "Richards")  {
	flow_dT = FPK->get_flow_dT();

        // adjust the time step, so that we exactly hit the switchover time
        if (ti_mode == INIT_TO_STEADY &&  S->get_time() < Tswitch && S->get_time()+flow_dT >= Tswitch) {
          limiter_dT = time_step_limiter(S->get_time(), flow_dT, Tswitch);
	  tslimiter = MPC_LIMITS;
        }
      

        // make sure we hit any of the reset times exactly (not in steady mode)
        if (! ti_mode == STEADY) {
          if (reset_times_.size() > 0) {
	    // first we find the next reset time
	    int next_time_index(-1);
	    for (int ii=0; ii<reset_times_.size(); ii++) {
	      if (S->get_time() < reset_times_[ii]) {
	        next_time_index = ii;
	        break;
	      }
	    }
	    if (next_time_index >= 0) {
	      // now we are trying to hit the next reset time exactly
              if (ti_mode == INIT_TO_STEADY) {
                if (reset_times_[next_time_index] != Tswitch) {
	          if (S->get_time()+2*flow_dT > reset_times_[next_time_index]) {
	            limiter_dT = time_step_limiter(S->get_time(), flow_dT, reset_times_[next_time_index]);
	            tslimiter = MPC_LIMITS;
	          }
                }
              } else {
                if (S->get_time()+2*flow_dT > reset_times_[next_time_index]) {
                  limiter_dT = time_step_limiter(S->get_time(), flow_dT, reset_times_[next_time_index]);
                  tslimiter = MPC_LIMITS;
                }
              }
	    }
	  }
        }
      }
	
      if (ti_mode == TRANSIENT || (ti_mode == INIT_TO_STEADY && S->get_time() >= Tswitch) ) {
        if (transport_enabled) {
          transport_dT = TPK->calculate_transport_dT();
        }
        if (chemistry_enabled) {
          chemistry_dT = CPK->max_time_step();
        }
      }

      
      // take the mpc time step as the min of all suggested time steps 
      mpc_dT = std::min( std::min( std::min(flow_dT, transport_dT), chemistry_dT), limiter_dT );

      // figure out who limits the time step
      if (mpc_dT == flow_dT) {
	tslimiter = FLOW_LIMITS;
      } else if (mpc_dT == transport_dT) {
	tslimiter = TRANSPORT_LIMITS;
      } else if (mpc_dT == chemistry_dT) {
	tslimiter = CHEMISTRY_LIMITS;
      } else if (mpc_dT == limiter_dT) {
	tslimiter = MPC_LIMITS;
      }
      
      if (ti_mode == INIT_TO_STEADY && S->get_time() >= Tswitch && S->get_last_time() < Tswitch) {
	mpc_dT = std::min( mpc_dT, dTtransient );
	tslimiter = MPC_LIMITS; 
      }

      // make sure we will hit the final time exactly
      if (ti_mode == INIT_TO_STEADY && S->get_time() > Tswitch && S->get_time()+2*mpc_dT > T1) { 
        mpc_dT = time_step_limiter(S->get_time(), mpc_dT, T1);
	tslimiter = MPC_LIMITS;
      } 
      if (ti_mode == TRANSIENT && S->get_time()+2*mpc_dT > T1) { 
	mpc_dT = time_step_limiter(S->get_time(), mpc_dT, T1);
	tslimiter = MPC_LIMITS;
      }
      if (ti_mode == STEADY  &&  S->get_time()+2*mpc_dT > T1) { 
	mpc_dT = time_step_limiter(S->get_time(), mpc_dT, T1);
	tslimiter = MPC_LIMITS;
      }
      
      // make sure that if we are currently on a reset time, to reset the time step
      if (! ti_mode == STEADY) {
	for (int ii=0; ii<reset_times_.size(); ++ii) {
	  // this is probably iffy...
	  if (S->get_time() == reset_times_[ii]) {
	    mpc_dT = reset_times_dt_[ii];
	    tslimiter = MPC_LIMITS;
	    // now reset the BDF2 integrator..
	    FPK->init_transient(S->get_time(), mpc_dT);   
	    break;	    
	  }
	}
      }




      // steady flow is special, it might redo a time step, so we print
      // time step info after we've advanced steady flow

      // first advance flow
      if (flow_enabled) {
	if (ti_mode == STEADY || (ti_mode == INIT_TO_STEADY && S->get_time() < Tswitch)) { 	
	  bool redo(false);
	  do {
	    redo = false;
	    try {
	      FPK->advance_steady(mpc_dT);
	    }
	    catch (int itr) {
	      mpc_dT = 0.5*mpc_dT;
	      redo = true;
	      tslimiter = FLOW_LIMITS;
	      *out << "will repeat time step with smaller dT = " << mpc_dT << std::endl;
	    }
	  } while (redo);
	} else {
	  FPK->advance_transient(mpc_dT);
	}
        FPK->commit_new_saturation(FS);
      }

      // =============================================================
      // write some info about the time step we are about to take
      
      // first determine what we will write about the time step limiter
      std::string limitstring("");
      switch (tslimiter) {
      case(MPC_LIMITS): 
	limitstring = std::string("(mpc limits timestep)");
	break;
      case (TRANSPORT_LIMITS): 
	limitstring = std::string("(transport limits timestep)");
	break;
      case (CHEMISTRY_LIMITS): 
	limitstring = std::string("(chemistry limits timestep)");
	break;
      case (FLOW_LIMITS): 
	limitstring = std::string("(flow limits timestep)");
	break;
      }

      if(out.get() && includesVerbLevel(verbLevel,Teuchos::VERB_LOW,true)) {
        *out << "Cycle = " << iter;
        *out << ",  Time(secs) = "<< S->get_time() / (365.25*60*60*24);
        *out << ",  dT(secs) = " << mpc_dT / (365.25*60*60*24);
	*out << " " << limitstring;
        *out << std::endl;
      }
      // ==============================================================

      // then advance transport
      if (ti_mode == TRANSIENT || (ti_mode == INIT_TO_STEADY && S->get_time() >= Tswitch) ) {
        if (transport_enabled) {
          TPK->advance( mpc_dT );
          if (TPK->get_transport_status() == AmanziTransport::TRANSPORT_STATE_COMPLETE) {
            // get the transport state and commit it to the state
            Teuchos::RCP<AmanziTransport::Transport_State> TS_next = TPK->get_transport_state_next();
            *total_component_concentration_star = *TS_next->get_total_component_concentration();
          } else {
            Errors::Message message("MPC: error... Transport_PK.advance returned an error status");
            Exceptions::amanzi_throw(message);
          }
        } else { // if we're not advancing transport we still need to prepare for chemistry
          *total_component_concentration_star = *S->get_total_component_concentration();
        }
      
      // then advance chemistry
        if (chemistry_enabled) {
          try {
            // now advance chemistry
            CPK->advance(mpc_dT, total_component_concentration_star);
            S->update_total_component_concentration(CPK->get_total_component_concentration());
          } catch (const ChemistryException& chem_error) {
            std::ostringstream error_message;
            error_message << "MPC: error... Chemistry_PK.advance returned an error status";
            error_message << chem_error.what();
            Errors::Message message(error_message.str());
            Exceptions::amanzi_throw(message);
          }
        } else {
          // commit total_component_concentration_star to the state and move on
          S->update_total_component_concentration(*total_component_concentration_star);
        }
      }
      
      // update the time in the state object
      S->advance_time(mpc_dT);


      // ===========================================================

      // we're done with this time step, commit the state
      // in the process kernels

      FPK->commit_state(FS,mpc_dT);
      if (ti_mode == TRANSIENT || (ti_mode == INIT_TO_STEADY && S->get_time() >= Tswitch) ) {
        if (transport_enabled) TPK->commit_state(TS);
        if (chemistry_enabled) CPK->commit_state(CS, mpc_dT);
      }

      // advance the iteration count
      iter++;
      S->set_cycle(iter);


      // make observations
      if (observations) observations->make_observations(*S);


      // write visualization if requested
      bool force(false);
      if ( abs(S->get_time() - T1) < 1e-7) { 
	force = true;
      }

      if ( ti_mode == INIT_TO_STEADY ) 
        if ( abs(S->get_time() - Tswitch) < 1e-7 ) {
          force = true;
        }

      if (chemistry_enabled) {
        // get the auxillary data
        Teuchos::RCP<Epetra_MultiVector> aux = CPK->get_extra_chemistry_output_data();
        
        // write visualization data for timestep if requested
        S->write_vis(*visualization, &(*aux), auxnames, force);
      } else {
        S->write_vis(*visualization, force);
      }

      // write restart dump if requested
      restart->dump_state(*S);
    }
    
  }

  // some final output
  if(out.get() && includesVerbLevel(verbLevel,Teuchos::VERB_LOW,true))
  {
    *out << "Cycle = " << iter;
    *out << ",  Time(secs) = "<< S->get_time()/ (365.25*60*60*24);
    *out << std::endl;
  }


}

double MPC::time_step_limiter (double T, double dT, double T_end) {

  double time_remaining = T_end - T;

  if (time_remaining < 0.0) {
    Errors::Message message("MPC: time step limiter logic error, T_end must be greater than T.");
    Exceptions::amanzi_throw(message);    
  }

  
  if (dT >= time_remaining) {
    return time_remaining;
  } else if ( dT > 0.75*time_remaining ) {
    return 0.5*time_remaining;
  } else {
    return dT;
  }
}

} // close namespace Amanzi
