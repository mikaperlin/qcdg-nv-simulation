#include <iostream> // for standard output
using namespace std;

#include <eigen3/Eigen/Dense> // linear algebra library
using namespace Eigen;

#include "constants.h"
#include "qp-math.h"
#include "nv-math.h"
#include "nv-control.h"

//--------------------------------------------------------------------------------------------
// Coordinate systems
//--------------------------------------------------------------------------------------------

// return "natural" basis of a nucleus
vector<Vector3d> natural_basis(const nv_system& nv, const uint index){
  const Vector3d target_zhat = hat(effective_larmor(nv,index));
  const Vector3d target_xhat = hat(hyperfine_perp(nv,index));
  const Vector3d target_yhat = target_zhat.cross(target_xhat);
  return {target_xhat, target_yhat, target_zhat};
}

//--------------------------------------------------------------------------------------------
// General control methods
//--------------------------------------------------------------------------------------------

// propagator U = exp(-i * rotation_angle * sigma_{axis}^{target})
MatrixXcd U_ctl(const nv_system& nv, const uint target, const double target_azimuth,
                const double rotation_angle, const bool exact, const bool adjust_AXY,
                const double z_phase){
  // identify cluster of target nucleus
  const uint cluster = get_cluster_containing_index(nv,target);
  const uint target_in_cluster = get_index_in_cluster(target,nv.clusters.at(cluster));
  const uint spins = nv.clusters.at(cluster).size()+1;

  // target axis of rotation
  const Vector3d axis_ctl = natural_axis(nv, target, target_azimuth);

  if(exact){
    // return exact propagator
    const MatrixXcd G = exp(-j * rotation_angle * dot(s_vec,axis_ctl));
    return act(G, {target_in_cluster+1}, spins);
  }

  // larmor frequency of target nucleus
  const double w_larmor = effective_larmor(nv,target).norm();
  const double t_larmor = 2*pi/w_larmor;

  // AXY protocol parameters
  const double sA = nv.scale_factor * hyperfine(nv,target).norm();
  const double w_DD = [&]() -> double {
    const double w_DD_large = (w_larmor+sA)/3.;
    if(w_larmor < sA){
      return w_DD_large;
    } else{
      const uint k_m = 2*int(0.5 * (w_larmor/sA-1) );
      const double w_DD_small = (w_larmor-sA)/k_m;
      if(w_DD_small > sA && isfinite(w_DD_small)){
        return w_DD_small;
      } else{
        return w_DD_large;
      }
    }
  }();

  const double t_DD = 2*pi/w_DD;
  const axy_harmonic k_DD = abs(w_DD - w_larmor) < abs(3*w_DD - w_larmor) ? first : third;
  const double f_DD = 0;

  const double dw_min = larmor_resolution(nv,target);
  double g_B_ctl = dw_min/nv.scale_factor; // control field strength * gyromangnetic ratio

  // frequency and period of phase rotation
  const double w_phase = g_B_ctl/4;
  const double t_phase = 2*pi/w_phase;

  // time for which to apply the control field
  double control_time = -rotation_angle/w_phase; // control operation time
  control_time -= floor(control_time/t_phase)*t_phase;
  if(control_time > t_phase/2){
    g_B_ctl *= -1;
    control_time = t_phase - control_time;
  }

  const double B_ctl = g_B_ctl/nv.nuclei.at(target).g; // control field strength
  const control_fields controls(B_ctl*axis_ctl, w_larmor); // control field object

  MatrixXcd U_ctl;
  if(!adjust_AXY){
    U_ctl = simulate_propagator(nv, cluster, w_DD, f_DD, k_DD, controls, control_time);
  } else{ // if(adjust_AXY)
    assert(w_DD != w_larmor);
    if(w_DD < w_larmor){
      const uint freq_ratio = 2*round(0.5*w_larmor/w_DD);
      const double w_DD_adjusted = w_larmor/freq_ratio;
      const double t_DD_adjusted = 2*pi/w_DD_adjusted;
      const uint cycles = int(control_time/t_DD_adjusted);

      const double leading_time = control_time - cycles*t_DD_adjusted;
      const double trailing_time = t_DD_adjusted - leading_time;

      const MatrixXcd U_leading = simulate_propagator(nv, cluster, w_DD_adjusted, f_DD, k_DD,
                                                      controls, leading_time);
      const MatrixXcd U_trailing = simulate_propagator(nv, cluster, w_DD_adjusted, f_DD, k_DD,
                                                       controls, trailing_time, leading_time);

      U_ctl = U_leading * pow(U_trailing*U_leading,cycles);
    } else{ // if(w_DD > w_larmor)
      const uint freq_ratio = round(w_DD/w_larmor);
      const double w_DD_adjusted = w_larmor*freq_ratio;
      const double t_DD_adjusted = 2*pi/w_DD_adjusted;
      const uint cycles = int(control_time/t_larmor);

      const double leading_time = control_time - cycles*t_larmor;
      const double trailing_time = t_larmor - leading_time;

      const MatrixXcd U_leading = simulate_propagator(nv, cluster, w_DD_adjusted, f_DD, k_DD,
                                                      controls, leading_time);
      const MatrixXcd U_trailing = simulate_propagator(nv, cluster, w_DD_adjusted, f_DD, k_DD,
                                                       controls, trailing_time, leading_time);

      U_ctl = U_leading * pow(U_trailing*U_leading,cycles);
    }
  }

  double flush_time = ceil(control_time/t_larmor)*t_larmor - control_time - z_phase/w_larmor;
  flush_time -= floor(flush_time/t_larmor)*t_larmor;
  const MatrixXcd U_flush =
    simulate_propagator(nv, cluster, w_DD, f_DD, k_DD, flush_time, control_time);

  return U_flush * U_ctl;
}

// compute and perform operationc necessary to act U on target nucleus
MatrixXcd act_target(const nv_system& nv, const uint target, const Matrix2cd& U,
                     const bool exact, const bool adjust_AXY){
  const uint cluster = get_cluster_containing_index(nv,target);
  const uint target_in_cluster = get_index_in_cluster(target,nv.clusters.at(cluster));
  const uint spins = nv.clusters.at(cluster).size()+1;

  if(exact){
    const MatrixXcd to_natural_axis = rotate({xhat,yhat,zhat},natural_basis(nv,target));
    return act(to_natural_axis.adjoint() * U * to_natural_axis, {target_in_cluster+1}, spins);
  }

  const Vector4cd H_vec = U_decompose(j*log(U));
  const double rx = real(H_vec(1))*2;
  const double ry = real(H_vec(2))*2;
  const double rz = real(H_vec(3))*2;

  const double rotation_angle = sqrt(rx*rx + ry*ry + rz*rz);
  if(rotation_angle == 0) return MatrixXcd::Identity(pow(2,spins),pow(2,spins));

  const double azimuth = atan2(ry,rx);
  const double pitch = asin(rz/rotation_angle);

  const double net_pole_rotation = pi - 2*abs(pitch);
  const double net_equatorial_rotation =
    2*abs(pitch) + (rotation_angle < pi ? rotation_angle : 2*pi - rotation_angle);

  if(net_pole_rotation < net_equatorial_rotation){
    const int pole = pitch > 0 ? 1 : -1; // "north" vs "south" pole
    const double angle_to_pole = pi/2 - abs(pitch);

    const MatrixXcd to_pole =
      U_ctl(nv, target, azimuth-pi/2, pole*angle_to_pole/2, exact, adjust_AXY);
    const MatrixXcd rotate = U_ctl(nv, target, 0, 0, exact, adjust_AXY, pole*rotation_angle);

    return to_pole.adjoint() * rotate * to_pole;

  } else{
    const MatrixXcd to_equator = U_ctl(nv, target, azimuth+pi/2, pitch/2, exact, adjust_AXY);
    const MatrixXcd rotate = U_ctl(nv, target, azimuth, rotation_angle/2, exact, adjust_AXY);

    return to_equator.adjoint() * rotate * to_equator;
  }
}

// perform given rotation on a target nucleus
MatrixXcd rotate_target(const nv_system& nv, const uint target, const Vector3d& rotation,
                        const bool exact, const bool adjust_AXY){
  return act_target(nv, target, rotate(rotation), exact, adjust_AXY);
}

// propagator U = exp(-i * rotation_angle * sigma_{n_1}^{NV}*sigma_{n_2}^{target})
MatrixXcd U_int(const nv_system& nv, const uint target, const Vector3d& nv_axis,
                const double target_azimuth, const double rotation_angle, const bool exact){
  // identify cluster of target nucleus
  const uint cluster = get_cluster_containing_index(nv,target);
  const uint target_in_cluster = get_index_in_cluster(target,nv.clusters.at(cluster));
  const uint spins = nv.clusters.at(cluster).size()+1;

  if(exact){
    // return exact propagator
    const Vector3d target_axis = natural_axis(nv,target,target_azimuth);
    const MatrixXcd G = exp(-j * rotation_angle *
                            tp(dot(s_vec,nv_axis), dot(s_vec,target_axis)));
    return act(G, {0,target_in_cluster+1}, spins);
  }

  // verify that we can address this nucleus
  if(round(4*dot(nv.nuclei.at(target).pos,ao)) == 0.){
    cout << "Cannot address nuclei without hyperfine coupling perpendicular to the NV axis: "
         << target << endl;
    return MatrixXcd::Identity(pow(2,spins),pow(2,spins));
  }

  // larmor frequency of and perpendicular component of hyperfine field at target nucleus
  const double w_larmor = effective_larmor(nv,target).norm();
  const double t_larmor = 2*pi/w_larmor;
  const double dw_min = larmor_resolution(nv,target);
  const Vector3d A_perp = hyperfine_perp(nv,target);

  // control fields and interaction vector
  Vector3d axis_ctl = hat(A_perp);
  double B_ctl = 0;
  control_fields controls;
  for(uint index: nv.clusters.at(cluster)){
    if(index == target) continue;
    if(is_larmor_pair(nv,index,target)){
      const Vector3d A_perp_alt = hyperfine_perp(nv,index);
      B_ctl = sqrt(nv.static_Bz * A_perp.norm()/nv.nuclei.at(target).g);
      axis_ctl = hat(A_perp - dot(A_perp,hat(A_perp_alt))*hat(A_perp_alt));

      controls.add(B_ctl*axis_ctl, w_larmor);
    }
  }
  const Vector3d A_int = dot(A_perp,axis_ctl)*axis_ctl;
  const double interaction_angle = asin(dot(hat(A_perp).cross(hat(A_int)),
                                            hat(effective_larmor(nv,target))));

  // AXY sequence parameters
  const double w_DD = w_larmor/nv.k_DD; // AXY protocol angular frequency
  const double t_DD = 2*pi/w_DD; // AXY protocol period
  double f_DD = min(dw_min/(A_int.norm()*nv.scale_factor), axy_f_max(nv.k_DD));

  // frequency and period of phase rotation
  const double w_phase = f_DD*A_int.norm()/8;
  const double t_phase = 2*pi/w_phase;

  // time for which to interact
  double interaction_time = nv.ms*rotation_angle/w_phase;
  interaction_time -= floor(interaction_time/t_phase)*t_phase;
  if(interaction_time > t_phase/2){
    f_DD *= -1;
    interaction_time = t_phase - interaction_time;
  }

  const uint cycles = int(interaction_time/t_DD);
  const double leading_time = interaction_time - cycles*t_DD;
  const double trailing_time = t_DD - leading_time;

  const double phase_advance = (interaction_angle - target_azimuth)/w_larmor;

  const MatrixXcd U_leading = simulate_propagator(nv, cluster, w_DD, f_DD, nv.k_DD,
                                                  controls, leading_time, phase_advance);
  const MatrixXcd U_trailing = simulate_propagator(nv, cluster, w_DD, f_DD, nv.k_DD,
                                                   controls, trailing_time,
                                                   leading_time + phase_advance);
  const MatrixXcd U_coupling = U_leading * pow(U_trailing*U_leading,cycles);

  // rotate NV coupling axis into its interaction axis (i.e. zhat)
  const MatrixXcd nv_axis_rotation = act_NV(nv,rotate(zhat,nv_axis),spins);

  // correct for larmor precession of the nucleus
  double z_phase = interaction_time*w_larmor;
  z_phase -= floor(z_phase/(2*pi))*2*pi;

  const double z_flush_phase = (z_phase < pi) ? z_phase : 2*pi - z_phase;
  const double w_ctl = nv.nuclei.at(target).g*B_ctl/2;
  const double total_time = interaction_time + z_flush_phase/w_larmor;
  double xy_phase = interaction_time*w_ctl;
  xy_phase -= floor(xy_phase/(2*pi))*2*pi;

  const Vector3d xy_axis = cos(target_azimuth)*xhat + sin(target_azimuth)*yhat;

  const MatrixXcd flush_target =
    act_target(nv, target, rotate(xy_axis,xy_phase)*rotate(zhat,z_phase));

  return flush_target * nv_axis_rotation.adjoint() * U_coupling * nv_axis_rotation;
}

//--------------------------------------------------------------------------------------------
// Specific operations
//--------------------------------------------------------------------------------------------

// iSWAP operation
MatrixXcd iSWAP(const nv_system& nv, const uint index, const bool exact){
  const double iswap_phase = -pi/4;
  const double xhat_azimuth = 0;
  const double yhat_azimuth = pi/2;
  return
    U_int(nv, index, xhat, xhat_azimuth, iswap_phase, exact) *
    U_int(nv, index, yhat, yhat_azimuth, iswap_phase, exact);
};

MatrixXcd SWAP_NVST(const nv_system& nv, const uint idx1, const uint idx2, const bool exact){
  // assert that both target nuclei are in the same cluster
  const vector<uint> cluster = nv.clusters.at(get_cluster_containing_index(nv,idx1));
  assert(in_vector(idx2,cluster));

  // define angles
  const double angle = pi/4;
  const double xhat_azimuth = 0;
  const double yhat_azimuth = pi/2;

  // compute components of SWAP_NVST
  const MatrixXcd Rz_NV = act_NV(nv, rotate(2*angle*zhat), cluster.size()+1);
  const MatrixXcd Rx_1 = U_ctl(nv, idx1, xhat_azimuth, angle, exact);
  const MatrixXcd Ry_1 = U_ctl(nv, idx1, yhat_azimuth, angle, exact);
  const MatrixXcd Rz_1 = Rx_1 * Ry_1 * Rx_1.adjoint();
  const MatrixXcd iSWAP_NV_1 =
    U_int(nv, idx1, xhat, xhat_azimuth, -angle, exact) *
    U_int(nv, idx1, yhat, yhat_azimuth, -angle, exact);
  const MatrixXcd cNOT_NV_1 =
    Rz_NV * Rx_1 * U_int(nv, idx1, zhat, xhat_azimuth, -angle, exact);
  const MatrixXcd E_NV_2 = U_int(nv, idx2, yhat, xhat_azimuth, -angle, exact);

  // combine componenets into full SWAP_NVST operation
  const MatrixXcd M = E_NV_2.adjoint() * iSWAP_NV_1 * Rz_1.adjoint() * Rz_NV;
  return M.adjoint() * cNOT_NV_1 * M;
}
