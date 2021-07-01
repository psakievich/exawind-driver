#include "AMRWind.h"
#include "NaluWind.h"
#include "mpi.h"
#include <filesystem>

#include "yaml-cpp/yaml.h"
#include "tioga.h"

int main(int argc, char** argv)
{
    if (argc != 2) {
        throw std::runtime_error("Usage: nalu_amr <inpfile>");
    }

    MPI_Init(&argc, &argv);
    int psize, prank;
    MPI_Comm_size(MPI_COMM_WORLD, &psize);
    MPI_Comm_rank(MPI_COMM_WORLD, &prank);

    // Temporarily only testing 2 MPI ranks
    if (psize != 2) {
        printf("This application is meant to be run with 2 processes.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    const int num_nalu_ranks = 1;

    int nalu_range[1][3] = {0, num_nalu_ranks - 1, 1};
    int amr_range[1][3] = {num_nalu_ranks, psize - 1, 1};

    MPI_Group world_group, nalu_group, amr_group;
    MPI_Comm_group(MPI_COMM_WORLD, &world_group);
    MPI_Group_range_incl(world_group, 1, nalu_range, &nalu_group);
    MPI_Group_range_incl(world_group, 1, amr_range, &amr_group);

    MPI_Comm amr_comm, nalu_comm;
    MPI_Comm_create(MPI_COMM_WORLD, amr_group, &amr_comm);
    MPI_Comm_create(MPI_COMM_WORLD, nalu_group, &nalu_comm);

    const std::string inpfile(argv[1]);
    const YAML::Node doc(YAML::LoadFile(inpfile));
    const YAML::Node node = doc["exawind"];

    const std::string amr_inp = node["amr_wind_inp"].as<std::string>();
    const std::string nalu_inp = node["nalu_wind_inp"].as<std::string>();

    std::filesystem::path fpath_amr_inp(amr_inp);
    const std::string amr_log =
        fpath_amr_inp.replace_extension(".log").string();
    std::ofstream out(amr_log);

    if (nalu_comm != MPI_COMM_NULL) exawind::NaluWind::initialize();
    if (amr_comm != MPI_COMM_NULL)
        exawind::AMRWind::initialize(amr_comm, amr_inp, out);

    {
        const auto nalu_vars = node["nalu_vars"].as<std::vector<std::string>>();
        const auto amr_cvars =
            node["amr_cell_vars"].as<std::vector<std::string>>();
        const auto amr_nvars =
            node["amr_node_vars"].as<std::vector<std::string>>();
        const int nsteps = node["num_timesteps"].as<int>();

        TIOGA::tioga tg;
        tg.setCommunicator(MPI_COMM_WORLD, prank, psize);

        exawind::NaluWind* nalu;
        exawind::AMRWind* awind;
        if (nalu_comm != MPI_COMM_NULL)
            nalu = new exawind::NaluWind(nalu_comm, nalu_inp, tg);
        if (amr_comm != MPI_COMM_NULL) awind = new exawind::AMRWind(tg);

        if (amr_comm != MPI_COMM_NULL) awind->init_prolog(true);
        if (nalu_comm != MPI_COMM_NULL) nalu->init_prolog(true);
        if (nalu_comm != MPI_COMM_NULL) nalu->pre_overset_conn_work();
        if (amr_comm != MPI_COMM_NULL) awind->pre_overset_conn_work();
        // if (amr_comm != MPI_COMM_NULL) // Need to understand why these are in
        // python but don't work here
        //  tg.preprocess_amr_data();
        tg.profile();
        tg.performConnectivity();
        // if (amr_comm != MPI_COMM_NULL) // Need to understand why these are in
        // python but don't work here
        //  tg.performConnectivityAMR();
        if (nalu_comm != MPI_COMM_NULL) nalu->post_overset_conn_work();
        if (amr_comm != MPI_COMM_NULL) awind->post_overset_conn_work();
        if (nalu_comm != MPI_COMM_NULL) nalu->init_epilog();
        if (amr_comm != MPI_COMM_NULL) awind->init_epilog();
        if (nalu_comm != MPI_COMM_NULL) nalu->prepare_solver_prolog();
        if (amr_comm != MPI_COMM_NULL) awind->prepare_solver_prolog();

        if (nalu_comm != MPI_COMM_NULL) nalu->register_solution(nalu_vars);
        if (amr_comm != MPI_COMM_NULL)
            awind->register_solution(amr_cvars, amr_nvars);
        // if (amr_comm != MPI_COMM_NULL) // Need to understand why these are in
        // python but don't work here
        //  tg.dataUpdate_AMR();
        if (nalu_comm != MPI_COMM_NULL) nalu->update_solution();
        if (amr_comm != MPI_COMM_NULL) awind->update_solution();

        if (nalu_comm != MPI_COMM_NULL) nalu->prepare_solver_epilog();
        if (amr_comm != MPI_COMM_NULL) awind->prepare_solver_epilog();

        MPI_Barrier(MPI_COMM_WORLD);

        if (amr_comm != MPI_COMM_NULL)
            amrex::Print(std::cout)
                << "Executing " << nsteps << " timesteps" << std::endl;
        for (int i = 0; i < nsteps; ++i) {
            if (nalu_comm != MPI_COMM_NULL) nalu->pre_advance_stage1();
            if (amr_comm != MPI_COMM_NULL) awind->pre_advance_stage1();

            if (nalu_comm != MPI_COMM_NULL) nalu->pre_overset_conn_work();
            if (amr_comm != MPI_COMM_NULL) awind->pre_overset_conn_work();
            // if (amr_comm != MPI_COMM_NULL) // Need to understand why these
            // are in python but don't work here
            //  tg.preprocess_amr_data();
            tg.profile();
            tg.performConnectivity();
            // if (amr_comm != MPI_COMM_NULL) // Need to understand why these
            // are in python but don't work here
            //  tg.performConnectivityAMR();
            if (nalu_comm != MPI_COMM_NULL) nalu->post_overset_conn_work();
            if (amr_comm != MPI_COMM_NULL) awind->post_overset_conn_work();

            if (nalu_comm != MPI_COMM_NULL) nalu->pre_advance_stage2();
            if (amr_comm != MPI_COMM_NULL) awind->pre_advance_stage2();

            if (nalu_comm != MPI_COMM_NULL) nalu->register_solution(nalu_vars);
            if (amr_comm != MPI_COMM_NULL)
                awind->register_solution(amr_cvars, amr_nvars);
            // if (amr_comm != MPI_COMM_NULL) // Need to understand why these
            // are in python but don't work here
            //  tg.dataUpdate_AMR();
            if (nalu_comm != MPI_COMM_NULL) nalu->update_solution();
            if (amr_comm != MPI_COMM_NULL) awind->update_solution();

            if (nalu_comm != MPI_COMM_NULL) nalu->advance_timestep();
            if (amr_comm != MPI_COMM_NULL) awind->advance_timestep();

            if (nalu_comm != MPI_COMM_NULL) nalu->post_advance();
            if (amr_comm != MPI_COMM_NULL) awind->post_advance();
            MPI_Barrier(MPI_COMM_WORLD);
        }
        if (nalu_comm != MPI_COMM_NULL) delete nalu;
        if (amr_comm != MPI_COMM_NULL) delete awind;
    }

    if (amr_comm != MPI_COMM_NULL) exawind::AMRWind::finalize();
    if (nalu_comm != MPI_COMM_NULL) exawind::NaluWind::finalize();
    MPI_Finalize();

    return 0;
}