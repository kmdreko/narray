////////////////////////////////////////////////////////////////////////////////
// FILE: narray.hpp
// DATE: 2014-07-30
// AUTH: Trevor Wilson <kmdreko@gmail.com>
// DESC: Defines an N-dimensional templated array class

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2018 Trevor Wilson
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef WILT_NARRAY_HPP
#define WILT_NARRAY_HPP

#include <memory>
// - std::allocator
#include <type_traits>
// - std::remove_const

#include "util.h"
#include "point.hpp"
#include "narraydataref.hpp"

namespace wilt
{
  template <class T, dim_t N> class NArrayIterator;
  // - defined in "narrayiterator.hpp"

  //////////////////////////////////////////////////////////////////////////////
  // This class is designed to access a sequence of data and to manipulate it in
  // an N-dimensional manner.
  // 
  // This class works by keeping a reference to a sequence of Ts that is shared
  // between NArrays to avoid copying data and allow separate slices and
  // subarrays to manipulate the same data. It also keeps a separate pointer to
  // the base of its own segment of the shared data.
  // 
  // An NArray will keep its own size of each dimension as well as its own step
  // values. By step value, I mean it keeps track of the distance to the next
  // value in the shared data along that dimension. Keeping this for each 
  // dimensions is what allows slicing, flipping, transposing, etc. without 
  // needing to copy data. Slicing works by dropping a dimension/step. Flipping
  // just negates its step value. Transposing swaps dimension/step values. 
  // Ranges and subarrays just reduce the dimensions. Each operation does adjust
  // its base pointer to ensure it still covers the proper segment of data.
  // 
  // NArray<int, 3>({ 4, 3, 2 })        creates an NArray like so:
  //   .rangeX(1, 3)
  //   .flipY()                         dimensions = {  3,  2,  3 }
  //   .transpose(1, 2);                steps      = {  6,  1, -2 }
  // x--x--x--x--x--x--2--5--1--4--0--3--8--11-7--10-6--9--14-17-13-16-12-15
  // |data                         |base
  // 
  // NArray<int, 2>({ 4, 6 })           creates an NArray like so:
  //   .sliceY(1);
  //                                    dimensions = {  4 }
  //                                    steps      = {  6 }
  // x--0--x--x--x--x--x--1--x--x--x--x--x--2--x--x--x--x--x--3--x--x--x--x
  // |data
  // 
  // These cases show how a single data-set can have multiple representations
  // but also how the data can be fragmented and unordered, but there are ways
  // to see and adjust it. For example, isContiguous() will return whether there
  // are gaps or not. The function isAligned() will return if the data would be
  // accessed in increasing order and aligned() will return an NArray with the
  // same view that would be accessed in increasing order.
  // 
  // As a side note, all manipulations are thread-safe; two threads can safely
  // use the same data-set, but modification of data is not protected.

  template <class T, dim_t N>
  class NArray
  {
  public:
    ////////////////////////////////////////////////////////////////////////////
    // TYPE DEFINITIONS
    ////////////////////////////////////////////////////////////////////////////

    typedef typename std::remove_const<T>::type type;
    typedef       T  value;
    typedef const T  cvalue;
    typedef       T* pointer;
    typedef const T* cpointer;
    typedef       T& reference;
    typedef const T& creference;

    typedef NArrayIterator<value, N> iterator;
    typedef NArrayIterator<cvalue, N> const_iterator;

    using slice_type = std::conditional_t<(N>1), NArray<T, N-1>, T&>;

  private:
    ////////////////////////////////////////////////////////////////////////////
    // PRIVATE MEMBERS
    ////////////////////////////////////////////////////////////////////////////

    NArrayDataRef<type> m_data; // reference to shared data
    type* m_base;               // base of current data segment
    Point<N> m_dims;            // dimension sizes
    Point<N> m_step;            // step sizes

  public:
    ////////////////////////////////////////////////////////////////////////////
    // CONSTRUCTORS
    ////////////////////////////////////////////////////////////////////////////

    // Default constructor, makes an empty NArray.
    NArray();

    // Creates an array of the given size, elements are default constructed.
    explicit NArray(const Point<N>& size);

    // Creates an array of the given size, elements are copy constructed from
    // 'val'.
    NArray(const Point<N>& size, const T& val);

    // Creates an array of the given size, elements are constructed or not 
    // based on 'type'
    //   - ASSUME: uses provided data, will delete when complete
    //   - COPY: copies the data
    //   - REF: uses provided data, will not delete when complete
    //
    // NOTE: ptr must be at least the size indicated by 'size'
    NArray(const Point<N>& size, T* ptr, PTR type);

    // Creates an array of the given size, elements are copy constructed from
    // the list
    //
    // NOTE: ptr must be at least the size indicated by size
    NArray(const Point<N>& size, std::initializer_list<T> list);

    // Creates an array of the given size, elements are constructed from the
    // result of 'gen()'
    //
    // NOTE: 'gen()' is called once per element
    template <class Generator>
    NArray(const Point<N>& size, Generator gen);

    // Copy constructor, shares data and uses the same data segment as 'arr'
    NArray(const NArray<T, N>& arr);

    // Copy constructor, assumes the same data segment as 'arr'
    //
    // NOTE: 'arr' is empty after being moved
    NArray(NArray<T, N>&& arr);

    // Copy constructor from 'T' to 'const T'
    template <class U, typename = std::enable_if<std::is_const<T>::value && std::is_same<U, std::remove_const<T>::type>::type>::type>
    NArray(const NArray<U, N>& arr);

    // Move constructor from 'T' to 'const T'
    //
    // NOTE: 'arr' is empty after being moved
    template <class U, typename = std::enable_if<std::is_const<T>::value && std::is_same<U, std::remove_const<T>::type>::type>::type>
    NArray(NArray<U, N>&& arr);

  public:
    ////////////////////////////////////////////////////////////////////////////
    // ASSIGNMENT OPERATORS
    ////////////////////////////////////////////////////////////////////////////

    //! @brief      Assignment operator. References the data from arr. Copies
    //!             all internal structures and increments internal reference
    //!             counter (if data exists). Deletes previous data if last
    //!             reference
    //! @param[in]  arr - NArray to copy from
    //! @return     reference to this object
    NArray<T, N>& operator= (const NArray<T, N>& arr);

    NArray<T, N>& operator= (NArray<T, N>&& arr);

    //! @brief      Assignment operator for const NArray. If T is also const,
    //!             then perform shallow copy and increment reference counter
    //!             (if data exists), otherwise construct new NArray of same 
    //!             size and deep copy. Deletes previous data if last reference
    //! @param[in]  arr - const NArray to copy from
    //! @return     reference to this object
    template <class U, typename = std::enable_if<std::is_const<T>::value && std::is_same<U, std::remove_const<T>::type>::type>::type>
    NArray<T, N>& operator= (const NArray<U, N>& arr);

    template <class U, typename = std::enable_if<std::is_const<T>::value && std::is_same<U, std::remove_const<T>::type>::type>::type>
    NArray<T, N>& operator= (NArray<U, N>&& arr);

    //! @brief      In-place addition operator, dimensions of both NArrays must
    //!             match, should trigger compiler error if attempted on const
    //!             type
    //! @param[in]  arr - NArray to add
    //! @return     reference to this object
    NArray<T, N>& operator+= (const NArray<cvalue, N>& arr)
    {
      static_assert(!std::is_const<T>::value, "operator+= invalid on const type");

      if (m_dims != arr.m_dims)
        throw std::invalid_argument("NArray+=(arr) dimensions must match");
      if (empty())
        return *this;

      unaryOp2_(m_base, arr.m_base, &m_dims[0], &m_step[0], &arr.m_step[0], [](T& lhs, const T& rhs){lhs+=rhs;}, N);

      return *this;
    }

    //! @brief      In-place addition operator, should trigger compiler error if
    //!             attempted on const type
    //! @param[in]  val - value to add to each element
    //! @return     reference to this object
    NArray<T, N>& operator+= (const T& val)
    {
      static_assert(!std::is_const<T>::value, "operator+= invalid on const type");

      if (empty())
        return *this;

      singleOp2_(m_base, &m_dims[0], &m_step[0], [&val](T& lhs){lhs+=val;}, N);

      return *this;
    }

    //! @brief      In-place subtraction operator, dimensions of both NArrays 
    //!             must match, should trigger compiler error if attempted on
    //!             const type
    //! @param[in]  arr - NArray to subtract
    //! @return     reference to this object
    NArray<T, N>& operator-= (const NArray<cvalue, N>& arr)
    {
      static_assert(!std::is_const<T>::value, "operator-= invalid on const type");

      if (m_dims != arr.m_dims)
        throw std::invalid_argument("NArray-=(arr) dimensions must match");
      if (empty())
        return *this;

      unaryOp2_(m_base, arr.m_base, &m_dims[0], &m_step[0], &arr.m_step[0], [](T& lhs, const T& rhs){lhs-=rhs;}, N);

      return *this;
    }

    //! @brief      In-place subtraction operator, should trigger compiler error
    //!             if attempted on const type
    //! @param[in]  val - value to subtract from each element
    //! @return     reference to this object
    NArray<T, N>& operator-= (const T& val)
    {
      static_assert(!std::is_const<T>::value, "operator-= invalid on const type");

      if (empty())
        return *this;

      singleOp2_(m_base, &m_dims[0], &m_step[0], [&val](T& lhs){lhs-=val;}, N);

      return *this;
    }

    //! @brief      In-place multiplication operator, should trigger compiler
    //!             error if attempted on const type
    //! @param[in]  val - value to multiply each element by
    //! @return     reference to this object
    NArray<T, N>& operator*= (const T& val)
    {
      static_assert(!std::is_const<T>::value, "operator*= invalid on const type");

      if (empty())
        return *this;

      singleOp2_(m_base, &m_dims[0], &m_step[0], [&val](T& lhs){lhs*=val;}, N);

      return *this;
    }

    //! @brief      In-place division operator, should trigger compiler error
    //!             if attempted on const type
    //! @param[in]  val - value to divide each element by
    //! @return     reference to this object
    NArray<T, N>& operator/= (const T& val)
    {
      static_assert(!std::is_const<T>::value, "operator/= invalid on const type");

      if (empty())
        return *this;

      singleOp2_(m_base, &m_dims[0], &m_step[0], [&val](T& lhs){lhs/=val;}, N);

      return *this;
    }

    //! @brief      Indexing operator to return an N-1 NArray at the x location
    //! @param[in]  x - the dimension 0 location
    //! @return     N-1 NArray referencing the same data
    //! @exception  std::out_of_range if x < length
    //! 
    //! Identical function to sliceX(x)
    //! It is preferable to do arr.at({x, y, y, ...}) than arr[x][y][z]... 
    //! because the operator[] must make a new NArray for each use.
    typename slice_type operator[] (pos_t x) const
    {
      if (x >= m_dims[0])
        throw std::out_of_range("operator[] index out of bounds");

      return sliceN_(x, 0);
    }

    //! @brief      Gets the dimensions of the NArray
    //! @return     Point containing the length of each dimension
    Point<N> dims() const
    {
      return m_dims;
    }

    //! @brief      Gets the step sizes of the NArray
    //! @return     Point containing the step sizes
    //!
    //! A step value is the offset from one element to the next, therefore the
    //! step sizes correspond the the step values for each dimension
    Point<N> step() const
    {
      return m_step;
    }

    //! @brief      Gets the number of elements accessible by the NArray
    //! @return     The size of the NArray
    pos_t size() const
    {
      return size_(m_dims);
    }

    //! @brief      Gets the length of the data in a dimension
    //! @param[in]  dim - the dimension to get the length of
    //! @return     the length of the dimension dim
    //! @exception  std::out_of_range if dim < N
    //! 
    //! Return value undefined for empty Mats; might be 0, last value, or junk.
    pos_t length(dim_t dim) const
    {
      if (dim >= N)
        throw std::out_of_range("length() argument out of bounds");

      return m_dims[dim];
    }

    //! @brief      Returns whether the NArray is empty
    //! @return     True if NArray references no data
    bool empty() const
    {
      return m_data.ptr_() == nullptr;
    }

    //! @brief      Gets the width of the NArray
    //! @return     the x dimension size of the NArray
    //!
    //! Equivalent to length(0). Refer to length() description for caveats.
    pos_t width() const
    {
      return m_dims[0];
    }

    //! @brief      Gets the height of the NArray
    //! @return     the y dimension size of the NArray
    //!
    //! Equivalent to length(1). Refer to length() description for caveats.
    //! static_assert triggered if N < 2
    pos_t height() const
    {
      static_assert(N >= 2, "height() only valid when N >= 2");

      return m_dims[1];
    }

    //! @brief      Gets the depth of the NArray
    //! @return     the z dimension size of the NArray
    //!
    //! Equivalent to length(2). Refer to length() description for caveats.
    //! static_assert triggered if N < 3
    pos_t depth() const
    {
      static_assert(N >= 3, "depth() only valid when N >= 3");

      return m_dims[2];
    }

    //! @brief      Gets the element at the location
    //! @param[in]  loc - the location of the element to get
    //! @return     reference to the element at the location
    //! @exception  std::runtime_error if NArray is empty
    //! @exception  std::out_of_range if loc element invalid
    //!
    //! Use arr.at({x, y, z, ...}) instead of arr[x][y][z][]... 
    reference at(const Point<N>& loc) const
    {
      if (empty()) 
        throw std::runtime_error("at() invalid call on empty NArray");

      T* ptr = m_base;
      for (dim_t i = 0; i < N; ++i)
        if (loc[i] >= m_dims[i] || loc[i] < 0)
          throw std::out_of_range("at() element larger then dimensions");
        else
          ptr += loc[i] * m_step[i];

      return *ptr;
    }

    //! @brief      Gets the N-1 dimension slice at the location
    //! @param[in]  x, y, z, w - the location to get the slice
    //! @return     N-1 NArray that references the same data
    //! @exception  std::out_of_range if x|y|z|w invalid
    //! @exception  std::domain_error if call for invalid N
    //!
    //! Equivalent to sliceN(x|y|z|w, n)
    typename slice_type sliceX(pos_t x) const
    {
      if (x >= m_dims[0] || x < 0)
        throw std::out_of_range("sliceX(x) index out of bounds");

      return sliceN_(x, 0);
    }
    NArray<value, N-1> sliceY(pos_t y) const
    {
      static_assert(N >= 2, "sliceY() only valid when N >= 2");

      if (y >= m_dims[1] || y < 0)
        throw std::out_of_range("sliceY(y) index out of bounds");

      return sliceN_(y, 1);
    }
    NArray<value, N-1> sliceZ(pos_t z) const
    {
      static_assert(N >= 3, "sliceZ() only valid when N >= 3");

      if (z >= m_dims[2] || z < 0)
        throw std::out_of_range("sliceZ(z) index out of bounds");

      return sliceN_(z, 2);
    }
    NArray<value, N-1> sliceW(pos_t w) const
    {
      static_assert(N >= 4, "sliceW() only valid when N >= 4");

      if (w >= m_dims[3] || w < 0)
        throw std::out_of_range("sliceW(w) index out of bounds");

      return sliceN_(w, 3);
    }

    //! @brief      Gets the N-1 dimension slice at the location
    //! @param[in]  n - the location to get the slice
    //! @param[in]  dim - the dimension of the slice
    //! @return     N-1 NArray that references the same data
    //! @exception  std::out_of_range if dim or n invalid
    typename slice_type sliceN(pos_t n, dim_t dim) const
    {
      if (dim >= N || n >= m_dims[dim] || n < 0)
        throw std::out_of_range("nSlice(n, dim) index out of bounds");

      return sliceN_(n, dim);
    }

    //! @brief      Gets the NArray at the position
    //! @param[in]  pos - position of the NArray, dimensionality must be less
    //!             than N and positive
    //! @return     the NArray at the position
    //! @exception  std::domain_error if incorrect M
    //! @exception  std::out_of_range if pos element invalid
    template <dim_t M>
    NArray<value, N-M> arrayAt(const Point<M>& pos) const
    {
      static_assert(M<N && M>0, "arrayAt(): pos is not less than N");

      type* base = m_base;
      for (dim_t i = 0; i < M; ++i)
        if (pos[i] >= m_dims[i] || pos[i] < 0)
          throw std::out_of_range("arrayAt(): pos out of range");
        else
          base += m_step[i] * pos[i];

      return NArray<value, N-M>(m_data, base, chopHigh_<N-M>(m_dims), chopHigh_<N-M>(m_step));
    }

    //! @brief      Gets a NArray within the range specified along the dimension
    //! @param[in]  x, y, z, w - the starting index of the range
    //! @param[in]  length - the length of the range
    //! @return     N-1 NArray that references the same data
    //! @exception  std::out_of_range if x|y|z|w invalid
    //! @exception  std::domain_error if call for invalid N
    //!
    //! Equivalent to rangeN(x|y|z|w, length, n)
    NArray<value, N> rangeX(pos_t x, pos_t length) const
    {
      if (x >= m_dims[0] || x+length > m_dims[0] || length <= 0 || x < 0)
        throw std::out_of_range("xRange(x, length) index out of bounds");

      return rangeN_(x, length, 0);
    }
    NArray<value, N> rangeY(pos_t y, pos_t length) const
    {
      static_assert(N >= 2, "rangeY() only valid when N >= 2");

      if (y >= m_dims[1] || y+length > m_dims[1] || length <= 0 || y < 0)
        throw std::out_of_range("yRange(y, length) index out of bounds");

      return rangeN_(y, length, 1);
    }
    NArray<value, N> rangeZ(pos_t z, pos_t length) const
    {
      static_assert(N >= 3, "rangeZ() only valid when N >= 3");

      if (z >= m_dims[2] || z+length > m_dims[2] || length <= 0 || z < 0)
        throw std::out_of_range("zRange(z, length) index out of bounds");

      return rangeN_(z, length, 2);
    }
    NArray<value, N> rangeW(pos_t w, pos_t length) const
    {
      static_assert(N >= 4, "rangeW() only valid when N >= 4");

      if (w >= m_dims[3] || w+length > m_dims[3] || length <= 0 || w < 0)
        throw std::out_of_range("wRange(w, length) index out of bounds");

      return rangeN_(w, length, 3);
    }

    //! @brief      Gets a NArray within the range specified along the dimension
    //! @param[in]  n - the starting index of the range
    //! @param[in]  length - the length of the range
    //! @param[in]  dim - the dimension of the range
    //! @return     N-1 NArray that references the same data
    //! @exception  std::out_of_range if dim, n, or length invalid
    NArray<value, N> rangeN(pos_t n, pos_t length, dim_t dim) const
    {
      if (n >= m_dims[dim] || n+length > m_dims[dim] || length <= 0 || n < 0 || dim >= N)
        throw std::out_of_range("nRange(n, length, dim) index out of bounds");

      return rangeN_(n, length, dim);
    }

    //! @brief      Gets an NArray withthe first two dimensions swapped
    //! @return     NArray that references the data
    //! @exception  std::domain_error if call for invalid N
    //!
    //! t() is identical to t(0, 1)
    NArray<value, N> transpose() const
    {
      static_assert(N >= 2, "t() only valid when N >= 2");

      return transpose(0, 1);
    }

    //! @brief      Gets an NArray with two dimensions swapped
    //! @param[in]  dim1 - first dimension to swap
    //! @param[in]  dim2 - second dimension to swap
    //! @return     NArray that references the same data
    //! @exception  std::out_of_range if dim1 or dim2 invalid
    NArray<value, N> t(dim_t dim1, dim_t dim2) const
    {
      if (dim1 >= N || dim2 >= N)
        throw std::out_of_range("t(dim1, dim2) index out of bounds");

      return NArray<value, N>(m_data, m_base, swap_(m_dims, dim1, dim2), swap_(m_step, dim1, dim2));
    }

    //! @brief      Gets an NArray with a dimension reversed
    //! @return     NArray that references the same data
    //! @exception  std::domain_error if call for invalid N
    //!
    //! Equivalent to flipN(0|1|2|3)
    NArray<value, N> flipX() const
    {
      return flipN_(0);
    }
    NArray<value, N> flipY() const
    {
      static_assert(N >= 2, "flipY() only valid when N >= 2");

      return flipN_(1);
    }
    NArray<value, N> flipZ() const
    {
      static_assert(N >= 3, "flipZ() only valid when N >= 3");

      return flipN_(2);
    }
    NArray<value, N> flipW() const
    {
      static_assert(N >= 4, "flipW() only valid when N >= 4");

      return flipN_(3);
    }

    //! @brief      Gets an NArray with a dimension reversed
    //! @param[in]  dim - the dimension to flip
    //! @return     NArray that references the same data
    //! @exception  std::out_of_range if dim1 or dim2 invalid
    NArray<value, N> flipN(dim_t dim) const
    {
      if (dim >= N)
        throw std::out_of_range("nFlip(dim) index out of bounds");

      return flipN_(dim);
    }

    NArray<value, N> subarray(const Point<N>& loc, const Point<N>& size) const
    {
      type* base = m_base;
      for (dim_t i = 0; i < N; ++i)
      {
        if (size[i]+loc[i] > m_dims[i] || size[i] <= 0 || loc[i] < 0 || loc[i] >= m_dims[i])
          throw std::out_of_range("subarray() index out of bounds");
        base += m_step[i] * loc[i];
      }

      return NArray<value, N>(m_data, base, size, m_step);
    }

    template <class Operator>
    void apply(Operator op)
    {
      singleOp2_((pointer)m_base, dimsPtr_(), stepPtr_(), op, N);
    }

    template <class Operator>
    void apply(Operator op) const
    {
      singleOp2_((cpointer)m_base, dimsPtr_(), stepPtr_(), op, N);
    }

    //! @brief      Converts the data to a new type using static_cast or custom
    //!             converter function
    //! @param[in]  func - the function or function object with the signature
    //!             U(const T&) or U(T)
    //! @return     NArray that references the converted data
    template <class U>
    NArray<U, N> convertTo() const
    {
      NArray<U, N> ret(m_dims);
      convertTo_(*this, ret, [](const T& t){return static_cast<U>(t);});
      return ret;
    }
    template <class U, class Converter>
    NArray<U, N> convertTo(Converter func) const
    {
      NArray<U, N> ret(m_dims);
      convertTo_(*this, ret, func);
      return ret;
    }

    //! @brief      Sets the data currently refenced to values in arr or a
    //!             constant
    //! @param[in]  arr - NArray to set values from, dimensions must match
    //! @param[in]  val - value to set to each element
    void setTo(const NArray<cvalue, N>& arr)
    {
      if (m_dims != arr.dims())
        throw std::invalid_argument("setTo(arr): dimensions must match");
      if (std::is_const<T>::value)
        throw std::domain_error("setTo(arr): cannot set to const type");

      unaryOp2_(m_base, arr.basePtr_(), dimsPtr_(), stepPtr_(), arr.stepPtr_(), 
        [](type& r, const type& v){r = v;}, N);
    }
    void setTo(const T& val)
    {
      if (std::is_const<T>::value)
        throw std::domain_error("setTo(val): cannot set to const type");

      singleOp2_(m_base, dimsPtr_(), stepPtr_(), [&val](type& r){r = val;}, N);
    }

    //! @brief      Sets the data currently refenced to values in arr or a
    //!             constant where the mask != 0
    //! @param[in]  arr - NArray to set values from, dimensions must match
    //! @param[in]  val - value to set to each element
    //! @param[in]  mask - uchar array where != 0 sets the corresponding value,
    //!             dimensions must match
    void setTo(const NArray<cvalue, N>& arr, const NArray<uint8_t, N>& mask)
    {
      if (m_dims != arr.dims() || m_dims != mask.dims())
        throw std::invalid_argument("setTo(arr, mask): dimensions must match");
      if (std::is_const<T>::value)
        throw std::domain_error("setTo(arr, mask): cannot set to const type");

      binaryOp2_(m_base, arr.basePtr_(), mask.basePtr_(), dimsPtr_(), stepPtr_(), arr.stepPtr_(), mask.stepPtr_(), 
        [](type& r, const type& v, uint8_t m){if (m != 0) r = v;}, N);
    }
    void setTo(const T& val, const NArray<uint8_t, N>& mask)
    {
      if (std::is_const<T>::value)
        throw std::domain_error("setTo(val): cannot set to const type");

      unaryOp2_(m_base, mask.basePtr_(), dimsPtr_(), stepPtr_(), mask.stepPtr_(), 
        [&val](type& r, uint8_t m){if (m != 0) r = val;}, N);
    }

    //! @brief      copies the data referenced into a new NArray
    //! @return     NArray that references a copy of the data
    NArray<type, N> clone() const
    {
      NArray<type, N> ret(m_dims);
      ret.setTo(*this);
      return ret;
    }

    //! @brief      creates a NArray that references the data in increasing order
    //!             in memory
    //! @return     NArray that references the data
    //!
    //! Usefull for traversing data efficiently when order doesn't matter
    NArray<value, N> aligned() const
    {
      Point<N> dims = m_dims;
      Point<N> step = m_step;
      pos_t offset = align_(dims, step);

      return NArray<value, N>(m_data, m_base + offset, dims, step);
    }

    //! @brief      clears this object, deallocates the data if its the last
    //!             reference
    void clear()
    {
      m_data.clear();
      m_base = nullptr;

      clean_();
    }

    //! @brief      returns whether the data is continuous
    //! @return     true if data is continuous, false if gaps exist
    //!
    //! if the NArray is continuous and aligned, you can traverse the data
    //! linearly starting from basePtr_()
    bool isContinuous() const
    {
      pos_t stepSize = 0;
      for (dim_t i = 0; i < N; ++i)
        stepSize += m_step[i]*(m_dims[i]-1);

      return stepSize+1 == this->size();
    }

    //! @brief      returns whether the data is part of a larger NArray
    //! @return     true if data accessible is less than data shared, false if
    //!             otherwise or if empty
    bool isSubarray() const
    {
      if (empty())
        return false;
      else
        return (std::size_t)size() < m_data->size;
    }

    //! @brief      returns whether the data is accessed linearly in memory
    //! @return     true if internal steps are positive and descreasing, false
    //!             otherwise
    //!
    //! if the NArray is continuous and aligned, you can traverse the data
    //! linearly starting from basePtr_()
    bool isAligned() const
    {
      for (dim_t i = 0; i < N; ++i)
        if (m_step[i] <= 0)
          return false;
      for (dim_t i = 1; i < N; ++i)
        if (m_step[i-1] < m_step[i])
          return false;
      return true;
    }

    //! @brief      returns whether the data is unique to this object
    //! @return     true if only this references the shared data, false if empty
    //!             or other references exist
    bool isUnique() const
    {
      return m_data.ptr_() && m_data.unique();
    }

    //! @brief      returns an iterator pointing to the beginning
    iterator begin()
    {
      return iterator(*this);
    }
    const_iterator begin() const
    {
      return const_iterator(*this);
    }
    const_iterator cbegin() const
    {
      return const_iterator(*this);
    }

    //! @brief      returns an iterator pointing past the end
    iterator end()
    {
      return iterator(*this, size());
    }
    const_iterator end() const
    {
      return const_iterator(*this, size());
    }
    const_iterator cend() const
    {
      return const_iterator(*this, size());
    }

    //! @brief      gets a pointer to the start of the referenced data
    pointer  basePtr_()
    {
      return m_base;
    }
    cpointer basePtr_() const
    {
      return m_base;
    }

    //! @brief      gets the pointer to the array array, size N
    const pos_t* dimsPtr_() const
    {
      return &m_dims[0];
    }

    //! @brief      gets the pointer of the step array, size N
    const pos_t* stepPtr_() const
    {
      return &m_step[0];
    }

    friend class NArray<value, N+1>;
    friend class NArray<typename std::toggle_const<T>::type, N>;
    friend class NArray<typename std::toggle_const<T>::type, N+1>;
    friend class NArrayIterator<type, N>;
    friend class NArrayIterator<cvalue, N>;

  protected:

    // Raw constructor
    NArray(NArrayDataRef<type> header, type* base, Point<N> dims, Point<N> step)
      : m_data(header),
        m_base(base),
        m_dims(dims),
        m_step(step)
    {
      
    }

  private:
    typename slice_type sliceN_(pos_t n, dim_t dim) const
    {
      return NArray<value, N-1>(m_data, m_base+m_step[dim]*n, slice_(m_dims, dim), slice_(m_step, dim));
    }
    NArray<value, N> rangeN_(pos_t n, pos_t length, dim_t dim) const
    {
      Point<N> temp = m_dims;
      temp[dim] = length;
      return NArray<value, N>(m_data, m_base+m_step[dim]*n, temp, m_step);
    }
    NArray<value, N> flipN_(dim_t dim) const
    {
      Point<N> temp = m_step;
      temp[dim] = -temp[dim];
      return NArray<value, N>(m_data, m_base+m_step[dim]*(m_dims[dim]-1), m_dims, temp);
    }

    bool valid_() const
    {
      for (dim_t i = 0; i < N; ++i)
        if (m_dims[i] == 0)
          return false;
        else if (m_dims[i] < 0)
          throw std::invalid_argument("dimension cannot be negative");
      return true;
    }

    void create_()
    {
      pos_t size = size_(m_dims);
      if (size > 0)
      {
        m_data = NArrayDataRef<type>(size);
        m_base = m_data->data;
      }
    }

    void create_(const T& val)
    {
      pos_t size = size_(m_dims);
      if (size > 0)
      {
        m_data = NArrayDataRef<type>(size, val);
        m_base = m_data->data;
      }
    }
    
    void create_(T* ptr, PTR ltype)
    {
      pos_t size = size_(m_dims);
      if (size > 0)
      {
        m_data = NArrayDataRef<type>(size, ptr, ltype);
        m_base = m_data->data;
      }
    }

    template <class Generator>
    void create_(Generator gen)
    {
      pos_t size = size_(m_dims);
      if (size > 0)
      {
        m_data = NArrayDataRef<type>(size, gen);
        m_base = m_data->data;
      }
    }

    void clean_()
    {
      m_dims.clear();
      m_step.clear();
    }

    template <class U, class Converter>
    static void convertTo_(const wilt::NArray<value, N>& lhs, wilt::NArray<U, N>& rhs, Converter func)
    {
      Point<N> dims = lhs.dims();
      Point<N> step1 = lhs.step();
      Point<N> step2 = rhs.step();
      dim_t n = condense_(dims, step1, step2);
      unaryOp_(rhs.basePtr_(), lhs.basePtr_(), &dims[0], &step2[0], &step1[0], func, n);
    }

  }; // class NArray

  template <class T>
  class NArray<T, 0>
  {
  public:
    typedef typename std::remove_const<T>::type type;

    NArray(const NArrayDataRef<T>& header, type* base, const Point<0>&, const Point<0>&)
      : m_data(header),
        m_base(base)
    {

    }

    operator T&() const
    {
      if (!m_data.ptr_())
        throw std::runtime_error("Mat<T, 0> references no data");

      return *m_base;
    }

  private:
    NArray() { }

    NArrayDataRef<T> m_data;
    type* m_base;

  }; // class NArray<T, 0>

    //! @brief         applies an operation on two source arrays and stores the
    //!                result in a destination array
    //! @param[out]    dst - the destination array
    //! @param[in]     src1 - pointer to 1st source array
    //! @param[in]     src2 - pointer to 2nd source array
    //! @param[in]     dims - pointer to dimension array
    //! @param[in]     dstep - pointer to dst step array
    //! @param[in]     s1step - pointer to 1st source step array
    //! @param[in]     s2step - pointer to 2nd source step array
    //! @param[in]     op - function or function object with the signature 
    //!                T(U, V) or similar
    //! @param[in]     N - the dimensionality of the arrays
    //!
    //! arrays must be the same size (hence the single dimension array), and
    //! this function makes no checks whether the inputs are valid.
  template <class T, class U, class V, class Operator>
  void binaryOp_(
    T* dst, const U* src1, const V* src2, const pos_t* dims,
    const pos_t* dstep, const pos_t* s1step, const pos_t* s2step,
    Operator op, dim_t N)
  {
    T* end = dst + dims[0] * dstep[0];
    if (N == 1)
    {
      for (; dst != end; dst += dstep[0], src1 += s1step[0], src2 += s2step[0])
        dst[0] = op(src1[0], src2[0]);
    }
    else
    {
      for (; dst != end; dst += dstep[0], src1 += s1step[0], src2 += s2step[0])
        binaryOp_(dst, src1, src2, dims + 1, dstep + 1, s1step + 1, s2step + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation on two source arrays and stores the
  //!                result in a destination array
  //! @param[in,out] dst - the destination array
  //! @param[in]     src1 - pointer to 1st source array
  //! @param[in]     src2 - pointer to 2nd source array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     dstep - pointer to dst step array
  //! @param[in]     s1step - pointer to 1st source step array
  //! @param[in]     s2step - pointer to 2nd source step array
  //! @param[in]     op - function or function object with the signature 
  //!                void(T&, U, V) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //!
  //! arrays must be the same size (hence the single dimension array), and
  //! this function makes no checks whether the inputs are valid.
  template <class T, class U, class V, class Operator>
  void binaryOp2_(
    T* dst, const U* src1, const V* src2, const pos_t* dims,
    const pos_t* dstep, const pos_t* s1step, const pos_t* s2step,
    Operator op, dim_t N)
  {
    T* end = dst + dims[0] * dstep[0];
    if (N == 1)
    {
      for (; dst != end; dst += dstep[0], src1 += s1step[0], src2 += s2step[0])
        op(dst[0], src1[0], src2[0]);
    }
    else
    {
      for (; dst != end; dst += dstep[0], src1 += s1step[0], src2 += s2step[0])
        binaryOp2_(dst, src1, src2, dims + 1, dstep + 1, s1step + 1, s2step + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation on a source array and stores the
  //!                result in a destination array
  //! @param[out]    dst - the destination array
  //! @param[in]     src - pointer to 1st source array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     dstep - pointer to dst step array
  //! @param[in]     sstep - pointer to source step array
  //! @param[in]     op - function or function object with the signature 
  //!                T(U) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //!
  //! arrays must be the same size (hence the single dimension array), and
  //! this function makes no checks whether the inputs are valid.
  template <class T, class U, class Operator>
  void unaryOp_(
    T* dst, const U* src, const pos_t* dims,
    const pos_t* dstep, const pos_t* sstep,
    Operator op, dim_t N)
  {
    T* end = dst + dstep[0] * dims[0];
    if (N == 1)
    {
      for (; dst != end; dst += dstep[0], src += sstep[0])
        dst[0] = op(src[0]);
    }
    else
    {
      for (; dst != end; dst += dstep[0], src += sstep[0])
        unaryOp_(dst, src, dims + 1, dstep + 1, sstep + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation on a source array and stores the
  //!                result in a destination array
  //! @param[in,out] dst - the destination array
  //! @param[in]     src - pointer to 1st source array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     dstep - pointer to dst step array
  //! @param[in]     sstep - pointer to source step array
  //! @param[in]     op - function or function object with the signature 
  //!                void(T&, U) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //!
  //! arrays must be the same size (hence the single dimension array), and
  //! this function makes no checks whether the inputs are valid.
  template <class T, class U, class Operator>
  void unaryOp2_(
    T* dst, const U* src, const pos_t* dims,
    const pos_t* dstep, const pos_t* sstep,
    Operator op, dim_t N)
  {
    T* end = dst + dstep[0] * dims[0];
    if (N == 1)
    {
      for (; dst < end; dst += dstep[0], src += sstep[0])
        op(dst[0], src[0]);
    }
    else
    {
      for (; dst < end; dst += dstep[0], src += sstep[0])
        unaryOp2_(dst, src, dims + 1, dstep + 1, sstep + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation and stores the result in a destination
  //!                array
  //! @param[out]    dst - the destination array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     dstep - pointer to dst step array
  //! @param[in]     op - function or function object with the signature 
  //!                T() or similar
  //! @param[in]     N - the dimensionality of the arrays
  //!
  //! this function makes no checks whether the inputs are valid.
  template <class T, class Operator>
  void singleOp_(
    T* dst, const pos_t* dims,
    const pos_t* dstep,
    Operator op, dim_t N)
  {
    T* end = dst + dims[0] * dstep[0];
    if (N == 1)
    {
      for (; dst != end; dst += dstep[0])
        dst[0] = op();
    }
    else
    {
      for (; dst != end; dst += dstep[0])
        singleOp_(dst, dims + 1, dstep + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation and stores the result in a destination
  //!                array
  //! @param[in,out] dst - the destination array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     dstep - pointer to dst step array
  //! @param[in]     op - function or function object with the signature 
  //!                void(T&) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //!
  //! this function makes no checks whether the inputs are valid.
  template <class T, class Operator>
  void singleOp2_(
    T* dst, const pos_t* dims,
    const pos_t* dstep,
    Operator op, dim_t N)
  {
    T* end = dst + dims[0] * dstep[0];
    if (N == 1)
    {
      for (; dst != end; dst += dstep[0])
        op(dst[0]);
    }
    else
    {
      for (; dst != end; dst += dstep[0])
        singleOp2_(dst, dims + 1, dstep + 1, op, N - 1);
    }
  }

  //! @brief         applies an operation between two source arrays and returns
  //!                a bool. Returns false upon getting the first false, or
  //!                or returns true if all operations return true
  //! @param[in]     src1 - pointer to 1st source array
  //! @param[in]     src2 - pointer to 2nd source array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     s1step - pointer to 1st source step array
  //! @param[in]     s2step - pointer to 2nd source step array
  //! @param[in]     op - function or function object with the signature 
  //!                bool(T, U) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //! @return        true if all operations return true, else false
  //!
  //! this function makes no checks whether the inputs are valid.
  template <class T, class U, class Operator>
  bool allOf_(
    const T* src1, const U* src2, const pos_t* dims,
    const pos_t* s1step, const pos_t* s2step,
    Operator op, dim_t N)
  {
    T* end = src1 + dims[0] * s1step[0];
    if (N == 1)
    {
      for (; src1 != end; src1 += s1step[0], src2 += s2step[0])
        if (!op(src1[0], src2[0]))
          return false;
    }
    else
    {
      for (; src1 != end; src1 += s1step[0], src2 += s2step[0])
        if (!allOf_(src1, src2, dims + 1, s1step + 1, s2step + 1, op, N - 1))
          return false;
    }
    return true;
  }

  //! @brief         applies an operation on a source array and returns a bool.
  //!                Returns false upon getting the first false, or returns true
  //!                if all operations return true
  //! @param[in]     src - pointer to source array
  //! @param[in]     dims - pointer to dimension array
  //! @param[in]     sstep - pointer to source step array
  //! @param[in]     op - function or function object with the signature 
  //!                bool(T) or similar
  //! @param[in]     N - the dimensionality of the arrays
  //! @return        true if all operations return true, else false
  //!
  //! this function makes no checks whether the inputs are valid.
  template <class T, class Operator>
  bool allOf_(
    const T* src, const pos_t* dims,
    const pos_t* sstep,
    Operator op, dim_t N)
  {
    T* end = src + dims[0] * sstep[0];
    if (N == 1)
    {
      for (; src != end; src += sstep[0])
        if (!op(src[0]))
          return false;
    }
    else
    {
      for (; src != end; src += sstep[0])
        if (!allOf_(src, dims + 1, sstep + 1, op, N - 1))
          return false;
    }
    return true;
  }

  //! @brief         applies an operation on two source arrays and stores the
  //!                result in a destination array
  //! @param[in]     src1 - 1st source array
  //! @param[in]     src2 - 2nd source array
  //! @param[in]     op - function or function object with the signature 
  //!                T(U, V) or similar
  //! @return        the destination array
  template <class T, class U, class V, dim_t N, class Operator>
  NArray<T, N> binaryOp(const NArray<U, N>& src1, const NArray<V, N>& src2, Operator op)
  {
    NArray<T, N> ret(src1.dims());
    binaryOp_(ret.basePtr_(), src1.basePtr_(), src2.basePtr_(), ret.dimsPtr_(), 
              ret.stepPtr_(), src1.stepPtr_(), src2.stepPtr_(), op, N);
    return ret;
  }

  //! @brief         applies an operation on two source arrays and stores the
  //!                result in a destination array
  //! @param[in,out] dst - the destination array
  //! @param[in]     src1 - 1st source array
  //! @param[in]     src2 - 2nd source array
  //! @param[in]     op - function or function object with the signature 
  //!                (T&, U, V) or similar
  template <class T, class U, class V, dim_t N, class Operator>
  void binaryOp(NArray<T, N>& dst, const NArray<U, N>& src1, const NArray<V, N>& src2, Operator op)
  {
    binaryOp2_(dst.basePtr_(), src1.basePtr_(), src2.basePtr_(), dst.dimsPtr_(), 
               dst.stepPtr_(), src1.stepPtr_(), src2.stepPtr_(), op, N);
  }

  //! @brief         applies an operation on a source array and stores the
  //!                result in a destination array
  //! @param[in]     src - pointer to 1st source array
  //! @param[in]     op - function or function object with the signature 
  //!                T(U) or similar
  //! @return        the destination array
  template <class T, class U, dim_t N, class Operator>
  NArray<T, N> unaryOp(const NArray<U, N>& src, Operator op)
  {
    NArray<T, N> ret(src.dims());
    unaryOp_(ret.basePtr_(), src.basePtr_(), ret.dimsPtr_(), 
             ret.stepPtr_(), src.stepPtr_(), op, N);
    return ret;
  }

  //! @brief         applies an operation on a source array and stores the
  //!                result in a destination array
  //! @param[in,out] dst - the destination array
  //! @param[in]     src - pointer to 1st source array
  //! @param[in]     op - function or function object with the signature 
  //!                (T&, U) or similar
  template <class T, class U, dim_t N, class Operator>
  void unaryOp(NArray<T, N>& dst, const NArray<U, N>& src, Operator op)
  {
    unaryOp2_(dst.basePtr_(), src.basePtr_(), dst.dimsPtr_(), 
              dst.stepPtr_(), src.stepPtr_(), op, N);
  }

  template <class T, dim_t N>
  typename sum_t<T>::type sum(const NArray<T, N>& src)
  {
    typename sum_t<T>::type sum = typename sum_t<T>::type();
    src.apply([&sum](const T& t){ sum += t; });
    return sum;
  }

  template <class T, dim_t N>
  T max(const NArray<T, N>& src)
  {
    typename std::remove_const<T>::type max = src.at(Point<N>());
    src.apply([&max](const T& t){ if (t > max) max = t; });
    return max;
  }

  template <class T, dim_t N>
  Point<N> maxAt(const NArray<T, N>& src)
  {
    typename std::remove_const<T>::type max = src.at(Point<N>());
    pos_t i = 0, idx = 0;
    src.apply([&max,&idx,&i](const T& t){ if (t > max) { max = t; idx = i; } ++i; });
    return idx2pos_(src.dims(), idx);
  }

  template <class T, dim_t N>
  T min(const NArray<T, N>& src)
  {
    typename std::remove_const<T>::type min = src.at(Point<N>());
    src.apply([&min](const T& t){ if (t < min) min = t; });
    return min;
  }

  template <class T, dim_t N>
  Point<N> minAt(const NArray<T, N>& src)
  {
    typename std::remove_const<T>::type min = src.at(Point<N>());
    pos_t i = 0, idx = 0;
    src.apply([&min,&idx,&i](const T& t){ if (t < min) { min = t; idx = i; } ++i; });
    return idx2pos_(src.dims(), idx);
  }

  template <class T, dim_t N>
  typename sum_t<T>::type mean(const NArray<T, N>& src)
  {
    return sum(src) / src.size();
  }

  template <class T, dim_t N>
  T median(const NArray<T, N>& src)
  {
    int n = src.size();
    const T** ptrs = new const T*[n];
    pos_t i = 0;
    src.apply([&i,&ptrs](const T& t){ ptrs[i] = &t; ++i; });

    // fastNth algorithm with N = n/2
    int A = 0;
    int B = n-1;
    while (A < B)
    {
      int a = A;
      int b = B;

      const T* x = ptrs[n/2];
      while (true)
      {
        while (*ptrs[a] < *x) ++a;
        while (*ptrs[b] > *x) --b;

        if (a > b) break;
        std::swap(ptrs[a++], ptrs[b--]);
      }

      if (b < n/2) A = a;
      if (a > n/2) B = b;
    }

    const T* ret = ptrs[n/2];
    delete[] ptrs;
    return *ret;
  }

  template <class T, dim_t N>
  pos_t count(const NArray<T, N>& src)
  {
    pos_t cnt = 0;
    src.apply([&cnt](const T& t){ if(t) ++cnt; });
    return cnt;
  }


  template <class T, dim_t N>
  NArray<T, N>::NArray()
    : m_data()
    , m_base(nullptr)
    , m_dims()
    , m_step()
  {

  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(const Point<N>& size)
    : m_data()
    , m_base(nullptr)
    , m_dims(size)
    , m_step(step_(size))
  {
    if (valid_())
      create_();
    else
      clean_();
  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(const Point<N>& size, const T& val)
    : m_data()
    , m_base(nullptr)
    , m_dims(size)
    , m_step(step_(size))
  {
    if (valid_())
      create_(val);
    else
      clean_();
  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(const Point<N>& size, T* ptr, PTR type)
    : m_data()
    , m_base(nullptr)
    , m_dims(size)
    , m_step(step_(size))
  {
    if (valid_())
      create_(ptr, type);
    else
      clean_();
  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(const Point<N>& size, std::initializer_list<T> list)
    : m_data()
    , m_base(nullptr)
    , m_dims(size)
    , m_step(step_(size))
  {
    if (valid_() && list.size() == size_(m_dims))
      create_((T*)list.begin(), PTR::COPY);
    else
      clean_();
  }

  template <class T, dim_t N>
  template <class Generator>
  NArray<T, N>::NArray(const Point<N>& size, Generator gen)
    : m_data()
    , m_base(nullptr)
    , m_dims(size)
    , m_step(step_(size))
  {
    if (valid_())
      create_(gen);
    else
      clean_();
  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(const NArray<T, N>& arr)
    : m_data(arr.m_data)
    , m_base(arr.m_base)
    , m_dims(arr.m_dims)
    , m_step(arr.m_step)
  {

  }

  template <class T, dim_t N>
  NArray<T, N>::NArray(NArray<T, N>&& arr)
    : m_data(arr.m_data)
    , m_base(arr.m_base)
    , m_dims(arr.m_dims)
    , m_step(arr.m_step)
  {
    arr.clear();
  }

  template <class T, dim_t N>
  template <class U, typename>
  NArray<T, N>::NArray(const NArray<U, N>& arr)
    : m_data()
    , m_base(nullptr)
    , m_dims(arr.m_dims)
    , m_step(arr.m_step)
  {
    if (std::is_const<T>::value)
    {
      m_data = arr.m_data;
      m_base = arr.m_base;
    }
    else
    {
      create_();
      setTo(arr);
    }
  }

  template <class T, dim_t N>
  template <class U, typename>
  NArray<T, N>::NArray(NArray<U, N>&& arr)
    : m_data()
    , m_base(nullptr)
    , m_dims(arr.m_dims)
    , m_step(arr.m_step)
  {
    if (std::is_const<T>::value || arr.isUnique())
    {
      m_data = arr.m_data;
      m_base = arr.m_base;
    }
    else
    {
      create_();
      setTo(arr);
    }

    arr.clear();
  }

  template <class T, dim_t N>
  NArray<T, N>& NArray<T, N>::operator= (const NArray<T, N>& arr)
  {
    m_data = arr.m_data;
    m_dims = arr.m_dims;
    m_step = arr.m_step;
    m_base = arr.m_base;

    return *this;
  }

  template <class T, dim_t N>
  NArray<T, N>& NArray<T, N>::operator= (NArray<T, N>&& arr)
  {
    m_data = arr.m_data;
    m_dims = arr.m_dims;
    m_step = arr.m_step;
    m_base = arr.m_base;

    arr.clear();

    return *this;
  }

  template <class T, dim_t N>
  template <class U, typename>
  NArray<T, N>& NArray<T, N>::operator= (const NArray<U, N>& arr)
  {
    m_dims = arr.m_dims;
    m_step = arr.m_step;

    if (std::is_const<T>::value)
    {
      m_data = arr.m_data;
      m_base = arr.m_base;
    }
    else
    {
      create_();
      setTo(arr);
    }

    return *this;
  }

  template <class T, dim_t N>
  template <class U, typename>
  NArray<T, N>& NArray<T, N>::operator= (NArray<U, N>&& arr)
  {
    m_dims = arr.m_dims;
    m_step = arr.m_step;

    if (std::is_const<T>::value || arr.isUnique())
    {
      m_data = arr.m_data;
      m_base = arr.m_base;
    }
    else
    {
      create_();
      setTo(arr);
    }

    return *this;
  }

} // namespace wilt

#include "narrayiterator.hpp"
#include "operators.hpp"

#endif // !WILT_NARRAY_HPP