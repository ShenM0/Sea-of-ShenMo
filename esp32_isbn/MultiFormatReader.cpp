/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0
// (1D-only, Arduino trimmed build)

#include "MultiFormatReader.h"

#include "BarcodeFormat.h"
#include "BinaryBitmap.h"
#include "ReaderOptions.h"
#include "ODReader.h"

#include <algorithm>
#include <memory>

namespace ZXing {

MultiFormatReader::MultiFormatReader(const ReaderOptions& opts) : _opts(opts)
{
        _readers.emplace_back(new OneD::Reader(opts));
}

MultiFormatReader::~MultiFormatReader() = default;

Result
MultiFormatReader::read(const BinaryBitmap& image) const
{
        Result r;
        for (const auto& reader : _readers) {
                r = reader->decode(image);
                if (r.isValid())
                        return r;
        }
        return _opts.returnErrors() ? r : Result();
}

Results MultiFormatReader::readMultiple(const BinaryBitmap& image, int maxSymbols) const
{
        std::vector<Result> res;

        for (const auto& reader : _readers) {
                if (image.inverted() && !reader->supportsInversion)
                        continue;
                auto r = reader->decode(image, maxSymbols);
                if (!_opts.returnErrors()) {
                        auto it = std::remove_if(res.begin(), res.end(), [](auto&& r) { return !r.isValid(); });
                        res.erase(it, res.end());
                }
                maxSymbols -= Size(r);
                res.insert(res.end(), std::move_iterator(r.begin()), std::move_iterator(r.end()));
                if (maxSymbols <= 0)
                        break;
        }

        std::sort(res.begin(), res.end(), [](const Result& l, const Result& r) {
                auto lp = l.position().topLeft();
                auto rp = r.position().topLeft();
                return lp.y < rp.y || (lp.y == rp.y && lp.x < rp.x);
        });

        return res;
}

} // ZXing
