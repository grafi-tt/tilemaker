#include <algorithm>
#include <stdexcept>
#include <vector>
#include <stdint.h>

template<class KeyT, class ValT>
class KeyValArrays {
	std::vector<KeyT> keys;
	std::vector<ValT> values;

public:
	ValT at(KeyT k) const {
		auto keyIt = std::lower_bound(keys.cbegin(), keys.cend(), k);
		if (keyIt == keys.cend() || *keyIt != k) {
			throw std::out_of_range("not found");
		}
		size_t idx = keyIt - keys.cbegin();
		return values[idx];
	}

	size_t count(KeyT k) const {
		return binary_search(keys.cbegin(), keys.cend(), k);
	}

	void insert_back(KeyT k, ValT v) {
		if (keys.size() > 0 && k <= keys.back()) {
			throw std::out_of_range("lnserting with a smaller key");
		}
		keys.emplace_back(k);
		values.emplace_back(v);
	}

	void clear() {
		*this = KeyValArrays<KeyT, ValT>();
	}
};

template<class KeyT, class ValT>
class IndexedKeyValArrays {
	std::vector<KeyT> keys;
	std::vector<size_t> indexes;
	std::vector<ValT> values;

public:
	IndexedKeyValArrays() : indexes({0}) {};

	std::pair<typename std::vector<ValT>::const_iterator, typename std::vector<ValT>::const_iterator> at(KeyT k) const {
		auto keyIt = lower_bound(keys.cbegin(), keys.cend(), k);
		if (keyIt == keys.cend() || *keyIt != k) {
			throw std::out_of_range("Not Found");
		}
		size_t rank = keyIt - keys.cbegin();
		auto beginIt = values.cbegin() + indexes[rank];
		auto endIt = values.cbegin() + indexes[rank+1];
		return { beginIt, endIt };
	}

	size_t count(KeyT k) const {
		return binary_search(keys.cbegin(), keys.cend(), k);
	}

	void insert_back(KeyT k, const std::vector<ValT> &vs) {
		if (keys.size() > 0 && k <= keys.back()) {
			throw std::out_of_range("inserting with a smaller key");
		}
		keys.emplace_back(k);
		values.insert(values.end(), vs.begin(), vs.end());
		indexes.emplace_back(values.size());
	}

	void clear() {
		*this = IndexedKeyValArrays<KeyT, ValT>();
	}
};
