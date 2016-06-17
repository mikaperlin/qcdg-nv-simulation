#define EIGEN_USE_MKL_ALL

#include <iostream> // for standard output
#include <vector> // vector objects
#include <eigen3/Eigen/Dense> // linear algebra library
#include <eigen3/unsupported/Eigen/KroneckerProduct> // provides tensor product
#include <eigen3/unsupported/Eigen/MatrixFunctions> // provides matrix functions

#include "constants.h"
#include "qp-math.h"

using namespace std;
using namespace Eigen;

// ---------------------------------------------------------------------------------------
// Matrix functions
// ---------------------------------------------------------------------------------------

// tensor product of many matrices
MatrixXcd tp(const initializer_list<MatrixXcd>& list) {
  MatrixXcd out = I1;
  for (MatrixXcd elem: list) {
    out = tp(out,elem);
  }
  return out;
}

// remove numerical artifacts from a matrix
MatrixXcd remove_artifacts(const MatrixXcd& A, const double threshold) {
  MatrixXcd B = MatrixXcd::Zero(A.rows(),A.cols());
  for (uint m = 0; m < A.rows(); m++) {
    for (uint n = 0; n < A.cols(); n++) {
      B(m,n) += round(A(m,n).real()/threshold)*threshold;
      B(m,n) += round(A(m,n).imag()/threshold)*threshold*j;
    }
  }
  return B;
}

// get global phase of matrix
complex<double> get_phase(const MatrixXcd& A, const double threshold) {
  for (uint m = 0; m < A.rows(); m++) {
    for (uint n = 0; n < A.cols(); n++) {
      if (abs(A(m,n)) > threshold) {
        const complex<double> phase = A(m,n)/abs(A(m,n));
        return phase;
      }
    }
  }
  return 1;
}

// ---------------------------------------------------------------------------------------
// Operator manipulation
// ---------------------------------------------------------------------------------------

// generate matrix B to act A on qbits qs_act out of qbits_new
MatrixXcd act(const MatrixXcd& A, const vector<uint>& qs_act, const uint qbits_new) {
  assert(A.rows() == A.cols()); // A should be square

  if (qs_act.size() == qbits_new) {
    bool do_nothing = true;
    for (uint i = 0; i < qbits_new; i++) {
      if (i != qs_act.at(i)) {
        do_nothing = false;
        break;
      }
    }
    if (do_nothing) return A;
  }

  // number of qbits A acted on
  const uint qbits_old = qs_act.size();
  assert(qbits_old == log2(A.rows()));

  // vector of qbits we are ignoring
  vector<uint> qs_ignore;
  for (uint i = 0; i < qbits_new; i++) {
    if (!in_vector(i,qs_act)) {
      qs_ignore.push_back(i);
    }
  }

  // initialize B (output) to the zero matrix
  MatrixXcd B = MatrixXcd::Zero(pow(2,qbits_new),pow(2,qbits_new));

  // loop over all entries A(m,n)
  for (uint m = 0; m < A.rows(); m++) {
    for (uint n = 0; n < A.cols(); n++) {

      // get contribution of substates |m-><n-| to indices of B
      uint b_m = 0, b_n = 0;
      for (uint q = 0; q < qbits_old; q++) {
        if (qbit_state(q,qbits_old,m)) b_m += bit_int(qs_act.at(q),qbits_new);
        if (qbit_state(q,qbits_old,n)) b_n += bit_int(qs_act.at(q),qbits_new);
      }

      // loop over all elements of the form |ms><ns| in B
      for (uint s = 0; s < pow(2,qs_ignore.size()); s++) {
        uint b_out = b_m, b_in = b_n;
        for (uint q = 0; q < qs_ignore.size(); q++) {
          if (qbit_state(q,qs_ignore.size(),s)) {
            b_out += bit_int(qs_ignore.at(q),qbits_new);
            b_in += bit_int(qs_ignore.at(q),qbits_new);
          }
        }
        B(b_out,b_in) = A(m,n);
      }
    }
  }
  return B;
}

// perform a partial trace over qbits qs_trace
MatrixXcd ptrace(const MatrixXcd& A, const vector<uint>& qs_trace) {
  assert(A.rows() == A.cols()); // A should be square

  // number of qbits A acted on
  const uint qbits_old = log2(A.rows());
  const uint qbits_new = qbits_old - qs_trace.size();

  // vector of qbits we are keeping
  vector<uint> qs_keep;
  for (uint i = 0; i < qbits_old; i++) {
    if (!in_vector(i,qs_trace)) qs_keep.push_back(i);
  }
  assert(qbits_new == qs_keep.size());

  // initialize B (output) to the zero matrix
  MatrixXcd B = MatrixXcd::Zero(pow(2,qbits_new),pow(2,qbits_new));

  // loop over all entries B(m,n)
  for (uint m = 0; m < B.rows(); m++) {
    for (uint n = 0; n < B.cols(); n++) {

      // get contribution of substates |m-><n-| to indices of A
      uint a_m = 0, a_n = 0;
      for (uint q = 0; q < qbits_new; q++) {
        if (qbit_state(q,qbits_new,m)) a_m += bit_int(qs_keep.at(q),qbits_old);
        if (qbit_state(q,qbits_new,n)) a_n += bit_int(qs_keep.at(q),qbits_old);
      }

      // loop over all elements of the form |ms><ns| in A
      for (uint s = 0; s < pow(2,qs_trace.size()); s++) {
        uint a_out = a_m, a_in = a_n;
        for (uint q = 0; q < qs_trace.size(); q++) {
          if (qbit_state(q,qs_trace.size(),s)) {
            a_out += bit_int(qs_trace.at(q),qbits_old);
            a_in += bit_int(qs_trace.at(q),qbits_old);
          }
        }
        B(m,n) += A(a_out,a_in);
      }
    }
  }
  return B;
}

// extract sub-matrix which acts only on the given qbits
MatrixXcd submatrix(const MatrixXcd& A, const vector<uint>& qbits) {
  assert(A.rows() == A.cols());

  const uint total_spins = log2(A.rows());
  vector<uint> ignored_qbits = {};
  for (uint n = 0; n < total_spins; n++) {
    if (!in_vector(n,qbits)) {
      ignored_qbits.push_back(n);
    }
  }

  MatrixXcd B = MatrixXcd::Identity(pow(2,qbits.size()),pow(2,qbits.size()));

  for (uint e = 0; e < pow(2,ignored_qbits.size()); e++) {
    uint A_start = 0;
    for (uint q = 0; q < ignored_qbits.size(); q++) {
      if (qbit_state(q, ignored_qbits.size(), e)) {
        A_start += bit_int(ignored_qbits.at(q), total_spins);
      }
    }

    MatrixXcd C = MatrixXcd::Identity(B.rows(),B.cols());

    for (uint C_row = 0; C_row < C.rows(); C_row++) {
      uint A_row = A_start;
      for (uint q = 0; q < qbits.size(); q++) {
        if (qbit_state(q, qbits.size(), C_row)) {
          A_row += bit_int(qbits.at(q), total_spins);
        }
      }

      for (uint C_col = 0; C_col < C.cols(); C_col++) {
        uint A_col = A_start;
        for (uint q = 0; q < qbits.size(); q++) {
          if (qbit_state(q, qbits.size(), C_col)) {
            A_col += bit_int(qbits.at(q), total_spins);
          }
        }

        C(C_row,C_col) = A(A_row,A_col);
      }
    }

    B += remove_phase(C);
  }

  B /= sqrt(real(trace(B.adjoint()*B)) / B.rows());

  return act(B,qbits,total_spins);
}

// ---------------------------------------------------------------------------------------
// Gate decomposition and fidelity
// ---------------------------------------------------------------------------------------

// returns basis element p for an operator acting on a system with N spins
MatrixXcd U_basis_element(const uint p, const uint N) {
  const vector<MatrixXcd> spins = {I2, sx, sy, sz};
  MatrixXcd b_p = I1;
  for (uint n = 0; n < N; n++) {
    b_p = tp(b_p,spins.at(int_bit(p,2*n)+2*int_bit(p,2*n+1)));
  }
  return b_p;
}

// returns element p of a basis for operators acting on a system with N qbits
string U_basis_element_text(const uint p, const uint N) {
  const vector<char> spins = {'I','X','Y','Z'};
  stringstream stream;
  for (uint n = 0; n < N; n++) {
    stream << spins.at(int_bit(p,2*n)+2*int_bit(p,2*n+1));
  }
  return stream.str();
}

// returns matrix whose columns are basis Hamiltonians for a system of N spins
MatrixXcd U_basis_matrix(const uint N) {
  MatrixXcd out = MatrixXcd::Zero(pow(4,N),pow(4,N));
  for (uint p = 0; p < pow(4,N); p++) {
    out.col(p) = flatten(U_basis_element(p,N));
  }
  return out;
}

// decompose an operator into its basis elements
VectorXcd U_decompose(const MatrixXcd& U, const bool fast) {
  const uint N = log2(U.rows());
  if (fast) return U_basis_matrix(N).partialPivLu().solve(flatten(U));
  else return U_basis_matrix(N).fullPivHouseholderQr().solve(flatten(U));
}

// compute mean fidelity of gate U with respect to G, i.e. how well U approximates G
double gate_fidelity(const MatrixXcd&  U, const MatrixXcd& G) {
  assert(U.size() == G.size());
  const uint D = U.rows();
  const MatrixXcd M = G.adjoint() * U;
  return real( trace(M.adjoint()*M) + trace(M)*conj(trace(M)) ) / (D*(D+1));
}

// compute mean fidelity of propagator acting on given qbits
double gate_fidelity(const MatrixXcd& U, const MatrixXcd& G,
                     const vector<uint>& system_qbits) {
  assert(U.size() == G.size());

  const uint spins = log2(G.rows());
  vector<uint> environment_qbits = {};
  for (uint n = 0; n < spins; n++) {
    if (!in_vector(n,system_qbits)) {
      environment_qbits.push_back(n);
    }
  }

  const MatrixXcd U_err = G.adjoint() * U;
  const MatrixXcd U_env = submatrix(U_err,environment_qbits);

  return gate_fidelity(U_env.adjoint()*U, G);
}

// ---------------------------------------------------------------------------------------
// Matrix vectors
// ---------------------------------------------------------------------------------------

mvec operator*(const MatrixXcd& G, const mvec& v) {
  vector<MatrixXcd> out;
  for (uint i = 0; i < v.size(); i++) {
    out.push_back(G*v.at(i));
  }
  return mvec(out);
}

// ---------------------------------------------------------------------------------------
// Printing methods
// ---------------------------------------------------------------------------------------

// print operator in human-readable form
void U_print(const MatrixXcd& U, const double threshold) {
  const int N = log2(U.rows());
  const VectorXcd hs = remove_artifacts(U_decompose(U), threshold);
  for (int p = 0; p < pow(4,N); p++) {
    if (abs(hs(p)) != 0) {
      cout << U_basis_element_text(p,N) << ": " << hs(p) << endl;
    }
  }
  cout << endl;
}

// print state vector in human readable form
void state_print(const MatrixXcd& psi) {
  const uint N = psi.size();
  const uint qbits = log2(N);
  for (uint n = 0; n < N; n++) {
    if (abs(psi(n)) != 0) {
      cout << "|";
      for (uint q = 0; q < qbits; q++) {
        cout << (qbit_state(q,qbits,n)?"d":"u");
      }
      cout << "> " << psi(n) << endl;
    }
  }
  cout << endl;
}

// print matrix in human readable form
void matrix_print(const MatrixXcd& M) {
  const uint qbits = log2(M.rows());
  for (uint m = 0; m < M.rows(); m++) {
    for (uint n = 0; n < M.cols(); n++) {
      if (abs(M(m,n)) != 0) {
        cout << "|";
        for (uint q = 0; q < qbits; q++) {
          cout << (qbit_state(q,qbits,m)?"d":"u");
        }
        cout << "><";
        for (uint q = 0; q < qbits; q++) {
          cout << (qbit_state(q,qbits,n)?"d":"u");
        }
        cout << "| " << M(m,n) << endl;
      }
    }
  }
  cout << endl;
}
