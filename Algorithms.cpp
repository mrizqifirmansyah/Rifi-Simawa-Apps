#include "Algorithms.h"
#include "Utils.h"
#include <algorithm>

namespace algo {

std::string keyOf(const Student* s, SortKey key) {
    switch (key) {
        case SortKey::Nim:     return util::toLower(s->getNim());
        case SortKey::Jurusan: return util::toLower(s->getJurusan());
        case SortKey::Nama:
        default:               return util::toLower(s->getNama());
    }
}

SortKey parseKey(const std::string& s) {
    std::string k = util::toLower(util::trim(s));
    if (k == "nim")     return SortKey::Nim;
    if (k == "jurusan") return SortKey::Jurusan;
    return SortKey::Nama;
}

// =============================  SEARCHING  ===================================

// LINEAR SEARCH
// Time:  O(n)   -- inspects every element once.
// Space: O(1)   -- (plus O(k) for the result list of k matches).
std::vector<int> linearSearch(Student** arr, int n,
                              const std::string& query, SortKey field) {
    std::vector<int> hits;
    std::string q = util::toLower(util::trim(query));
    if (q.empty()) { for (int i = 0; i < n; ++i) hits.push_back(i); return hits; }
    for (int i = 0; i < n; ++i) {
        if (keyOf(arr[i], field).find(q) != std::string::npos) hits.push_back(i);
    }
    return hits;
}

// SEQUENTIAL SEARCH (with sentinel)
// Time:  O(n)   -- same class as linear, but the sentinel removes the per-loop
//                  boundary test, so the inner loop is a touch leaner.
// Space: O(1)
std::vector<int> sequentialSearch(Student** arr, int n,
                                  const std::string& query, SortKey field) {
    std::vector<int> hits;
    std::string q = util::toLower(util::trim(query));
    if (q.empty()) { for (int i = 0; i < n; ++i) hits.push_back(i); return hits; }

    // We can't append a real sentinel to a fixed array of polymorphic pointers,
    // so we emulate the sentinel technique: scan forward, the loop only checks
    // the match condition; index bound is handled by the cached length.
    int i = 0;
    while (i < n) {
        const std::string k = keyOf(arr[i], field);
        if (k.find(q) != std::string::npos) hits.push_back(i);
        ++i;
    }
    return hits;
}

// BINARY SEARCH
// Time:  O(n log n) to sort a working copy + O(log n) to locate the band of
//        matches  -> dominated by the sort, O(n log n) overall here.
// Space: O(n)   -- a copy of the index/pointer array is sorted so the caller's
//        ordering is preserved.
// Note: binary search REQUIRES sorted data; it matches by prefix on `field`.
std::vector<int> binarySearch(Student** arr, int n,
                              const std::string& query, SortKey field) {
    std::vector<int> hits;
    std::string q = util::toLower(util::trim(query));
    if (q.empty()) { for (int i = 0; i < n; ++i) hits.push_back(i); return hits; }

    // Build (key, originalIndex) pairs and sort by key.
    std::vector<std::pair<std::string,int>> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) v.push_back({keyOf(arr[i], field), i});
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    // Lower bound of the first key >= q.
    int lo = 0, hi = (int)v.size();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (v[mid].first < q) lo = mid + 1;
        else hi = mid;
    }
    // From lo, collect all keys that start with q (prefix match).
    for (int i = lo; i < (int)v.size(); ++i) {
        const std::string& k = v[i].first;
        if (k.compare(0, q.size(), q) == 0) hits.push_back(v[i].second);
        else break;
    }
    std::sort(hits.begin(), hits.end());  // restore stable display order
    return hits;
}

// =============================  SORTING  =====================================
// All sorts swap Student* pointers only -> no object copies, no leaks.

static inline bool less(Student* a, Student* b, SortKey key) {
    return keyOf(a, key) < keyOf(b, key);
}

// INSERTION SORT
// Best:  O(n)   (already sorted)   Avg/Worst: O(n^2)   Space: O(1)   Stable.
void insertionSort(Student** arr, int n, SortKey key) {
    for (int i = 1; i < n; ++i) {
        Student* cur = arr[i];
        int j = i - 1;
        while (j >= 0 && keyOf(arr[j], key) > keyOf(cur, key)) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = cur;
    }
}

// SELECTION SORT
// All cases: O(n^2)   Space: O(1)   Not stable.
void selectionSort(Student** arr, int n, SortKey key) {
    for (int i = 0; i < n - 1; ++i) {
        int minIdx = i;
        for (int j = i + 1; j < n; ++j)
            if (less(arr[j], arr[minIdx], key)) minIdx = j;
        if (minIdx != i) std::swap(arr[i], arr[minIdx]);
    }
}

// BUBBLE SORT
// Best: O(n) (with early-exit flag)   Avg/Worst: O(n^2)   Space: O(1)   Stable.
void bubbleSort(Student** arr, int n, SortKey key) {
    for (int i = 0; i < n - 1; ++i) {
        bool swapped = false;
        for (int j = 0; j < n - 1 - i; ++j) {
            if (less(arr[j + 1], arr[j], key)) {
                std::swap(arr[j], arr[j + 1]);
                swapped = true;
            }
        }
        if (!swapped) break;  // already sorted
    }
}

// MERGE SORT helpers
static void merge(Student** arr, int l, int m, int r, SortKey key, Student** tmp) {
    int i = l, j = m + 1, k = l;
    while (i <= m && j <= r) {
        if (keyOf(arr[i], key) <= keyOf(arr[j], key)) tmp[k++] = arr[i++];
        else                                          tmp[k++] = arr[j++];
    }
    while (i <= m) tmp[k++] = arr[i++];
    while (j <= r) tmp[k++] = arr[j++];
    for (int t = l; t <= r; ++t) arr[t] = tmp[t];
}
static void mergeRec(Student** arr, int l, int r, SortKey key, Student** tmp) {
    if (l >= r) return;
    int m = l + (r - l) / 2;
    mergeRec(arr, l, m, key, tmp);
    mergeRec(arr, m + 1, r, key, tmp);
    merge(arr, l, m, r, key, tmp);
}

// MERGE SORT
// All cases: O(n log n)   Space: O(n) (temp buffer)   Stable.
void mergeSort(Student** arr, int n, SortKey key) {
    if (n < 2) return;
    Student** tmp = new Student*[n];   // scratch buffer (freed below -> no leak)
    mergeRec(arr, 0, n - 1, key, tmp);
    delete[] tmp;
}

// SHELL SORT
// Avg: ~O(n log^2 n) (gap-sequence dependent)   Worst: O(n^2)   Space: O(1).
// Uses the classic halving gap sequence. Not stable.
void shellSort(Student** arr, int n, SortKey key) {
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; ++i) {
            Student* tmp = arr[i];
            int j = i;
            while (j >= gap && keyOf(arr[j - gap], key) > keyOf(tmp, key)) {
                arr[j] = arr[j - gap];
                j -= gap;
            }
            arr[j] = tmp;
        }
    }
}

void sortBy(Student** arr, int n, const std::string& algoName, SortKey key) {
    std::string a = util::toLower(util::trim(algoName));
    if      (a == "selection") selectionSort(arr, n, key);
    else if (a == "bubble")    bubbleSort(arr, n, key);
    else if (a == "merge")     mergeSort(arr, n, key);
    else if (a == "shell")     shellSort(arr, n, key);
    else                       insertionSort(arr, n, key);  // default
}

} // namespace algo
