#pragma once

// Subsequence fuzzy matcher with VS Code/Sublime-style scoring.
// Header-only so it can be used by both the macOS and Windows editor
// dialogs without adding a translation unit to multiple build systems.
//
// Usage:
//     auto r = seFuzzy::match(L"osc", L"Phase Dist Osc");
//     if (r.matched) { ... r.score, r.positions ... }
//
// Scoring (per matched character):
//     +10 base
//     +50 if the match is at position 0 of the candidate
//     +30 if the match is at a word boundary (after space/punct or
//         the start of an upper-case run inside a lower-case word)
//     +15 if the match is consecutive with the previous matched char
// The final result is normalised by candidate length so that shorter
// candidates rank above longer ones for the same set of matches.

#include <algorithm>
#include <climits>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace seFuzzy {

struct MatchResult
{
    bool             matched{false};
    int              score{0};
    std::vector<int> positions;  // ascending indices into the candidate
};

namespace detail {

inline bool isWordBoundary(wchar_t prev, wchar_t curr)
{
    if (std::iswupper(curr) && !std::iswupper(prev) && std::iswalpha(prev))
        return true;
    return prev == L' ' || prev == L'\t' || prev == L'\\' || prev == L'/'
        || prev == L'_' || prev == L'-' || prev == L'.'  || prev == L'('
        || prev == L'[' || prev == L'{';
}

inline int matchBonus(int j, std::wstring_view candidate)
{
    int bonus = 10;
    if (j == 0)
        bonus += 50;
    else if (isWordBoundary(candidate[j - 1], candidate[j]))
        bonus += 30;
    return bonus;
}

}  // namespace detail

inline MatchResult match(std::wstring_view query, std::wstring_view candidate)
{
    MatchResult r;
    if (query.empty())
    {
        r.matched = true;
        return r;
    }

    const int Q = static_cast<int>(query.size());
    const int C = static_cast<int>(candidate.size());
    if (C < Q)
        return r;

    std::wstring qLower; qLower.reserve(Q);
    for (auto c : query)     qLower.push_back(static_cast<wchar_t>(std::towlower(c)));
    std::wstring cLower; cLower.reserve(C);
    for (auto c : candidate) cLower.push_back(static_cast<wchar_t>(std::towlower(c)));

    // Dynamic programming over (i,j) where i = query chars consumed,
    // j = candidate chars considered. We pick the best scoring alignment.
    // parent[i][j]: 0 = reached via skip, 1 = reached via match.
    constexpr int NEG_INF = INT_MIN / 2;
    const int W = C + 1;
    std::vector<int>  dp(static_cast<size_t>(Q + 1) * W, NEG_INF);
    std::vector<char> parent(static_cast<size_t>(Q + 1) * W, -1);
    auto idx = [W](int i, int j) { return static_cast<size_t>(i) * W + j; };

    for (int j = 0; j <= C; ++j)
        dp[idx(0, j)] = 0;

    for (int i = 1; i <= Q; ++i)
    {
        const wchar_t qc = qLower[i - 1];
        for (int j = 1; j <= C; ++j)
        {
            const int skipScore = dp[idx(i, j - 1)];
            if (skipScore > dp[idx(i, j)])
            {
                dp[idx(i, j)]     = skipScore;
                parent[idx(i, j)] = 0;
            }

            if (cLower[j - 1] == qc)
            {
                const int prev = dp[idx(i - 1, j - 1)];
                if (prev != NEG_INF)
                {
                    int b = detail::matchBonus(j - 1, candidate);
                    if (i >= 2 && j >= 2 && parent[idx(i - 1, j - 1)] == 1)
                        b += 15;
                    const int s = prev + b;
                    if (s > dp[idx(i, j)])
                    {
                        dp[idx(i, j)]     = s;
                        parent[idx(i, j)] = 1;
                    }
                }
            }
        }
    }

    const int best = dp[idx(Q, C)];
    if (best == NEG_INF)
        return r;

    r.matched = true;
    r.score   = best - (C - Q);  // length normalisation

    r.positions.reserve(Q);
    int i = Q, j = C;
    while (i > 0 && j > 0)
    {
        const char p = parent[idx(i, j)];
        if (p == 1)
        {
            r.positions.push_back(j - 1);
            --i;
            --j;
        }
        else if (p == 0)
        {
            --j;
        }
        else
        {
            break;
        }
    }
    std::reverse(r.positions.begin(), r.positions.end());

    return r;
}

}  // namespace seFuzzy
