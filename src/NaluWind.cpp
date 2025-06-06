#include "NaluWind.h"
#include "NaluEnv.h"
#include "Realm.h"
#include "TimeIntegrator.h"
#include "overset/ExtOverset.h"
#include "overset/TiogaRef.h"

#include "Kokkos_Core.hpp"
#include "tioga.h"
#include "HypreNGP.h"

namespace exawind {

void NaluWind::initialize()
{
    Kokkos::initialize();
    // Hypre initialization
    nalu_hypre::hypre_initialize();
    nalu_hypre::hypre_set_params();
}

void NaluWind::finalize()
{
    // Hypre cleanup
    nalu_hypre::hypre_finalize();

    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
}

NaluWind::NaluWind(
    int id,
    stk::ParallelMachine comm,
    const YAML::Node& inp_yaml,
    const std::string& logfile,
    const std::vector<std::string>& fnames,
    TIOGA::tioga& tg)
    : m_doc(inp_yaml), m_sim(m_doc), m_fnames(fnames), m_id(id), m_comm(comm)
{
    auto& env = sierra::nalu::NaluEnv::self();
    env.parallelCommunicator_ = comm;
    MPI_Comm_size(comm, &env.pSize_);
    MPI_Comm_rank(comm, &env.pRank_);

    ::tioga_nalu::TiogaRef::self(&tg);

    env.set_log_file_stream(logfile);
}

NaluWind::~NaluWind() = default;

void NaluWind::init_prolog(bool multi_solver_mode)
{
    // Dump the input yaml to the start of the logfile
    // before the nalu banner
    auto& env = sierra::nalu::NaluEnv::self();
    env.naluOutputP0() << std::string(20, '#') << " INPUT FILE START "
                       << std::string(20, '#') << std::endl;
    sierra::nalu::NaluParsingHelper::emit(*env.naluLogStream_, m_doc);
    env.naluOutputP0() << std::string(20, '#') << " INPUT FILE END   "
                       << std::string(20, '#') << std::endl;

    m_sim.load(m_doc);
    if (m_sim.timeIntegrator_->overset_ != nullptr)
        m_sim.timeIntegrator_->overset_->set_multi_solver_mode(
            multi_solver_mode);
    m_sim.breadboard();
    m_sim.init_prolog();
}

void NaluWind::init_epilog() { m_sim.init_epilog(); }

void NaluWind::prepare_solver_prolog()
{
    m_sim.timeIntegrator_->prepare_for_time_integration();
}

void NaluWind::prepare_solver_epilog()
{
    for (auto* realm : m_sim.timeIntegrator_->realmVec_)
        realm->output_converged_results();
}

void NaluWind::pre_advance_stage0(size_t inonlin)
{
    m_sim.timeIntegrator_->prepare_time_step(inonlin);
}

void NaluWind::pre_advance_stage1(size_t inonlin)
{
    m_sim.timeIntegrator_->pre_realm_advance_stage1(inonlin);
}

void NaluWind::pre_advance_stage2(size_t inonlin)
{
    m_sim.timeIntegrator_->pre_realm_advance_stage2(inonlin);
}

double NaluWind::get_time()
{
    return m_sim.timeIntegrator_->get_current_time();
}

double NaluWind::get_timestep_size()
{
    return m_sim.timeIntegrator_->get_time_step();
}

void NaluWind::set_timestep_size(const double dt)
{
    m_sim.timeIntegrator_->set_time_step(dt);
}

bool NaluWind::is_fixed_timestep_size()
{
    return m_sim.timeIntegrator_->get_is_fixed_time_step();
}

void NaluWind::advance_timestep(size_t /*inonlin*/)
{
    for (auto* realm : m_sim.timeIntegrator_->realmVec_) {
        realm->advance_time_step();
        realm->process_multi_physics_transfer();
    }
}

void NaluWind::additional_picard_iterations(const int n)
{
    for (auto* realm : m_sim.timeIntegrator_->realmVec_)
        realm->nonlinear_iterations(n);
}

void NaluWind::post_advance() { m_sim.timeIntegrator_->post_realm_advance(); }

void NaluWind::pre_overset_conn_work()
{
    m_sim.timeIntegrator_->overset_->pre_overset_conn_work();
}

void NaluWind::post_overset_conn_work()
{
    m_sim.timeIntegrator_->overset_->post_overset_conn_work();
}

void NaluWind::register_solution()
{
    m_ncomps = m_sim.timeIntegrator_->overset_->register_solution(m_fnames);
}

void NaluWind::update_solution()
{
    m_sim.timeIntegrator_->overset_->update_solution();
}

int NaluWind::overset_update_interval()
{
    for (auto& realm : m_sim.timeIntegrator_->realmVec_) {
        if (realm->does_mesh_move()) {
            return 1;
        }
    }
    return 100000000;
}

int NaluWind::time_index() { return m_sim.timeIntegrator_->timeStepCount_; }

void NaluWind::dump_simulation_time()
{
    for (auto& realm : m_sim.timeIntegrator_->realmVec_) {
        realm->dump_simulation_time();
    }
}

} // namespace exawind
