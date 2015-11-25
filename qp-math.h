#pragma once

using namespace std;

#include <eigen3/Eigen/Dense> // linear algebra library
#include <eigen3/unsupported/Eigen/KroneckerProduct> // provides tensor product
#include <eigen3/unsupported/Eigen/MatrixFunctions> // provides matrix functions
using namespace Eigen;

// check whether value val is in vector vec
inline bool in_vector(auto val, vector<auto> vec){
  return (find(vec.begin(), vec.end(), val) != vec.end());
}

// identity matrices
const MatrixXcd I1 = MatrixXcd::Identity(1,1);
const MatrixXcd I2 = MatrixXcd::Identity(2,2);
const MatrixXcd I4 = MatrixXcd::Identity(4,4);

// return unit vector in direction of vec
inline Vector3d hat(const Vector3d& vec){ return vec.normalized(); }

// matrix functions
inline complex<double> trace(const MatrixXcd& M){ return M.trace(); }
inline MatrixXcd log(const MatrixXcd& M){ return M.log(); }
inline MatrixXcd exp(const MatrixXcd& M){ return M.exp(); }
inline MatrixXcd sqrt(const MatrixXcd& M){ return M.sqrt(); }
inline MatrixXcd pow(const MatrixXcd& M, auto x){ return M.pow(x); }

// tensor product of two matrices
inline MatrixXcd tp(const MatrixXcd A, const MatrixXcd B){ return kroneckerProduct(A,B); }

// tensor product of many matrices
MatrixXcd tp(const initializer_list<MatrixXcd> list);

// remove numerical artifacts from a matrix
void remove_artifacts(MatrixXcd& A, double threshold = 1e-12);

// get global phase of matrix
complex<double> get_phase(const MatrixXcd& A);

// remove global phase from matrix
inline void remove_phase(MatrixXcd& A){ A *= conj(get_phase(A)); }

//--------------------------------------------------------------------------------------------
// Operator rearrangement
//--------------------------------------------------------------------------------------------

// get the n-th bit of an integer num
inline bool int_bit(uint num, uint n){
  if(pow(2,n) > num) return 0;
  else return (num >> n)&  1;
}

// get state of qbit q (of N) from enumerated state s
inline bool qbit_state(uint q, uint N, uint s){ return int_bit(s,N-1-q); }

// get integer corresponding to an 'on' state of bit p (of N)
inline uint bit_int(uint q, int N){ return pow(2,N-1-q); }

// generate matrix B to act A on qbits qs_act out of qbits_new
MatrixXcd act(const MatrixXcd& A, const vector<uint> qs_act, uint qbits_new);

// perform a partial trace over qbits qs_trace
MatrixXcd ptrace(const MatrixXcd& A, const vector<uint> qs_trace);

//--------------------------------------------------------------------------------------------
// Matrix vectors
//--------------------------------------------------------------------------------------------

struct mvec{
  vector<MatrixXcd> v;

  mvec(){};
  mvec(const vector<MatrixXcd> v){ this->v = v; };
  mvec(const MatrixXcd v_mat, const Vector3d v_vec){
    for(uint i = 0; i < v_vec.size(); i++){
      v.push_back(v_mat*v_vec(i));
    }
  };

  uint size() const { return v.size(); }
  MatrixXcd at(uint i) const { return v.at(i); }

  // comparison operators
  bool operator==(const mvec& w) const {
    assert(v.size() == w.size());
    for(uint i = 0; i < v.size(); i++){
      if(v.at(i) != w.at(i)) return false;
    }
    return true;
  }
  bool operator!=(const mvec& w) const { return !(*this == w); }

  // addition, subtraction, and multiplication
  mvec operator+(const mvec& w){
    assert(v.size() == w.size());
    for(uint i = 0; i < v.size(); i++){
      v.at(i) += w.at(i);
    }
    return *this;
  }
  mvec operator-(const mvec& w){
    assert(v.size() == w.size());
    for(uint i = 0; i < v.size(); i++){
      v.at(i) -= w.at(i);
    }
    return *this;
  }
  mvec operator*(double s){
    for(uint i = 0; i < v.size(); i++){
      v.at(i) *= s;
    }
    return *this;
  }
  mvec operator/(double s) { return *this * (1/s); }
  mvec operator*(const MatrixXcd& G){
    for(uint i = 0; i < v.size(); i++){
      v.at(i) *= G;
    }
    return *this;
  }

  // inner product with vectors and matrix vectors
  MatrixXcd dot(const Vector3d& r) const {
    assert(v.size() == 3);
    return v.at(0)*r(0) + v.at(1)*r(1) + v.at(2)*r(2);
  }
  MatrixXcd dot(const mvec& w) const {
    assert(v.size() == w.size());
    MatrixXcd out = tp(v.at(0),w.at(0));
    for(uint i = 1; i < v.size(); i++){
      out += tp(v.at(i),w.at(i));
    }
    return out;
  }
};

inline MatrixXcd dot(const mvec& v, const mvec& w){ return v.dot(w); }
inline MatrixXcd dot(const mvec& v, const Vector3d& r){ return v.dot(r); }
inline MatrixXcd dot(const Vector3d& r, const mvec& v){ return v.dot(r); }
inline double dot(const Vector3d& v, const Vector3d& w){ return v.dot(w); }

inline mvec operator*(double s, mvec& v){ return v*s; }
mvec operator*(const MatrixXcd& G, const mvec& v);
