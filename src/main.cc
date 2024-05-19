#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

using sz = std::size_t;
using u8 = std::uint8_t;
using i32 = std::int32_t;
using WordDB = std::vector<std::vector<std::string>>;

constexpr static auto MAX_NICKNAME_LEN = static_cast<sz>(12);
constexpr static auto NUM_INITIAL_NICKNAMES = static_cast<sz>(10'000'000);
constexpr static auto NUM_TRIES = static_cast<sz>(50'000'000);

std::filesystem::path get_wordlist_txt_path() noexcept
{
    auto const project_dir = std::filesystem::path(__FILE__).parent_path().parent_path();
    return project_dir / "external" / "wordlist" / "wordlist-20210729.txt";
}

WordDB load_word_db(std::string const &path)
{
    auto f = std::ifstream(path);
    if (!f.is_open())
    {
        spdlog::critical("failed to open word list file");
        std::exit(-1);
    }

    auto db = WordDB(static_cast<sz>(MAX_NICKNAME_LEN + 1));
    for (auto line = std::string(); !std::getline(f, line).eof();)
    {
        if (line.length() - 2 < db.size())
        {
            db[line.length() - 2].emplace_back(std::string_view(line.c_str() + 1, line.length() - 2));
        }
    }
    return db;
}

template <typename RandomEngine>
char sample_ascii(RandomEngine &random_engine)
{
    constexpr auto NUM_DIGITS = '9' - '0' + 1;
    constexpr auto NUM_ALPHAS = ('Z' - 'A' + 1) * 2;
    auto ch = std::uniform_int_distribution<int>(0, NUM_ALPHAS + NUM_DIGITS - 1)(random_engine);
    if (ch < NUM_DIGITS)
    {
        return static_cast<char>('0' + ch);
    }
    ch -= NUM_DIGITS;
    return static_cast<char>(ch <= 'Z' - 'A' ? ('A' + ch) : ('a' + (ch - ('Z' - 'A' + 1))));
}

template <typename RandomEngine>
char sample_digit(RandomEngine &random_engine)
{
    auto const ch = std::uniform_int_distribution<int>('0', '9')(random_engine);
    return static_cast<char>(ch);
}

template <typename RandomEngine>
char sample_ascii_lower(RandomEngine &random_engine)
{
    auto const ch = std::uniform_int_distribution<int>('a', 'z')(random_engine);
    return static_cast<char>(ch);
}

template <typename RandomEngine>
std::string sample_word(RandomEngine &random_engine, WordDB const &word_db, sz const min_len, sz const max_len)
{
    auto it = std::ranges::next(std::ranges::cbegin(word_db), min_len);
    auto const num_candidates = std::reduce(it, std::ranges::next(it, max_len - min_len + 1), static_cast<sz>(0),
                                            [](auto const total, auto const &it) { return total + it.size(); });
    if (num_candidates == 0)
    {
        constexpr auto msg = "there are no words to sample";
        spdlog::critical(msg);
        throw std::runtime_error(msg);
    }

    auto idx = std::uniform_int_distribution<sz>(0, num_candidates - 1)(random_engine);
    while (it->size() <= idx)
    {
        idx -= (it++)->size();
    }
    return it->at(idx);
}

template <typename RandomEngine>
std::string sample_and_mangle_word(RandomEngine &random_engine, WordDB const &word_db, sz const min_len,
                                   sz const max_len, double const mangling_factor)
{
    auto piece = sample_word(random_engine, word_db, min_len, max_len);
    auto indices = std::vector<sz>(piece.length());
    std::iota(std::ranges::begin(indices), std::ranges::end(indices), 0);
    auto magling_magnitude = static_cast<i32>(std::round(piece.length() / mangling_factor));
    while (0 < magling_magnitude--)
    {
        auto idx = std::uniform_int_distribution<sz>(0, indices.size() - 1)(random_engine);
        std::swap(indices[idx], indices.back());
        idx = indices.back();
        indices.pop_back();
        if (idx == 0)
        {
            piece[idx] = sample_ascii_lower(random_engine);
        }
        else
        {
            piece[idx] = static_cast<char>(std::tolower(sample_ascii(random_engine)));
        }
    }
    return piece;
}

struct SampleNicknameOpt
{
    sz min_len;
    sz max_len;
    sz min_word_len;
    sz max_word_len;
};

static auto const SAMPLE_NICKNAME_OPT = SampleNicknameOpt{8, 8, 3, 8};

template <typename RandomEngine>
std::string sample_nickname(RandomEngine &random_engine, WordDB const &word_db, SampleNicknameOpt const &opt)
{
    auto pieces = std::vector<std::string>();
    auto chance = std::uniform_int_distribution<sz>(opt.min_len, opt.max_len)(random_engine);
    while (0 < chance)
    {
        if (chance < opt.min_word_len)
        {
            auto piece = std::string();
            piece.push_back(sample_ascii_lower(random_engine));
            for (--chance; 0 < chance; --chance)
            {
                piece.push_back(sample_ascii(random_engine));
            }
            pieces.emplace_back(std::move(piece));
        }
        else
        {
            auto piece = sample_and_mangle_word(random_engine, word_db, opt.min_word_len,
                                                std::min(opt.max_word_len, chance), 2.7);
            chance -= piece.length();
            piece[0] = static_cast<char>(std::toupper(piece[0]));
            pieces.emplace_back(std::move(piece));
        }
    }

    std::shuffle(std::ranges::begin(pieces), std::ranges::end(pieces), random_engine);
    return std::accumulate(std::ranges::cbegin(pieces), std::ranges::cend(pieces), std::string(),
                           [](auto const &nickname, auto const &piece) { return nickname + piece; });
}

// --------------------------------------------------------------------------------------------------
// 32bit random engine 재사용
// --------------------------------------------------------------------------------------------------
void case01(WordDB const &word_db, std::tuple<sz, sz, double> &out)
{
    auto random_device = std::random_device();
    auto pesudo_random_engine = std::mt19937(random_device());

    auto nickname_db = std::unordered_set<std::string>();
    while (nickname_db.size() < NUM_INITIAL_NICKNAMES)
    {
        nickname_db.emplace(sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT));
    }

    auto num_collisions = sz();
    for (auto i = sz(); i < NUM_TRIES; ++i)
    {
        auto const nickname = sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT);
        if (nickname_db.contains(nickname))
        {
            ++num_collisions;
        }
    }

    std::get<0>(out) = NUM_TRIES;
    std::get<1>(out) = num_collisions;
    std::get<2>(out) = static_cast<double>(num_collisions) / NUM_TRIES * 100;
}

// --------------------------------------------------------------------------------------------------
// 32bit random engine 재생성
// --------------------------------------------------------------------------------------------------
void case02(WordDB const &word_db, std::tuple<sz, sz, double> &out)
{
    auto nickname_db = std::unordered_set<std::string>();
    while (nickname_db.size() < NUM_INITIAL_NICKNAMES)
    {
        auto random_device = std::random_device();
        auto pesudo_random_engine = std::mt19937(random_device());
        nickname_db.emplace(sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT));
    }

    auto num_collisions = sz();
    for (auto i = sz(); i < NUM_TRIES; ++i)
    {
        auto random_device = std::random_device();
        auto pesudo_random_engine = std::mt19937(random_device());
        auto const nickname = sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT);
        if (nickname_db.contains(nickname))
        {
            ++num_collisions;
        }
    }

    std::get<0>(out) = NUM_TRIES;
    std::get<1>(out) = num_collisions;
    std::get<2>(out) = static_cast<double>(num_collisions) / NUM_TRIES * 100;
}

// --------------------------------------------------------------------------------------------------
// 64bit random engine 재사용
// --------------------------------------------------------------------------------------------------
void case03(WordDB const &word_db, std::tuple<sz, sz, double> &out)
{
    auto random_device = std::random_device();
    auto pesudo_random_engine = std::mt19937_64(random_device());

    auto nickname_db = std::unordered_set<std::string>();
    while (nickname_db.size() < NUM_INITIAL_NICKNAMES)
    {
        nickname_db.emplace(sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT));
    }

    auto num_collisions = sz();
    for (auto i = sz(); i < NUM_TRIES; ++i)
    {
        auto const nickname = sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT);
        if (nickname_db.contains(nickname))
        {
            ++num_collisions;
        }
    }

    std::get<0>(out) = NUM_TRIES;
    std::get<1>(out) = num_collisions;
    std::get<2>(out) = static_cast<double>(num_collisions) / NUM_TRIES * 100;
}

// --------------------------------------------------------------------------------------------------
// 64bit random engine 재생성
// --------------------------------------------------------------------------------------------------
void case04(WordDB const &word_db, std::tuple<sz, sz, double> &out)
{
    auto nickname_db = std::unordered_set<std::string>();
    while (nickname_db.size() < NUM_INITIAL_NICKNAMES)
    {
        auto random_device = std::random_device();
        auto pesudo_random_engine = std::mt19937_64(random_device());
        nickname_db.emplace(sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT));
    }

    auto num_collisions = sz();
    for (auto i = sz(); i < NUM_TRIES; ++i)
    {
        auto random_device = std::random_device();
        auto pesudo_random_engine = std::mt19937_64(random_device());
        auto const nickname = sample_nickname(pesudo_random_engine, word_db, SAMPLE_NICKNAME_OPT);
        if (nickname_db.contains(nickname))
        {
            ++num_collisions;
        }
    }

    std::get<0>(out) = NUM_TRIES;
    std::get<1>(out) = num_collisions;
    std::get<2>(out) = static_cast<double>(num_collisions) / NUM_TRIES * 100;
}

void print_about_expriment_env(WordDB const &word_db)
{
    auto word_db_info = std::stringstream();
    word_db_info << "ENV: WORD DB { ";
    for (auto i = sz(); i < word_db.size(); ++i)
    {
        if (0 < i)
        {
            word_db_info << ", ";
        }
        word_db_info << '[' << i << ']' << '=' << word_db[i].size();
    }
    word_db_info << " }";
    spdlog::info(word_db_info.str());
}

int main([[maybe_unused]] int const argc, [[maybe_unused]] char const *const argv[])
{
    auto const word_db = [argc, argv]() {
        return argc == 1 ? load_word_db(get_wordlist_txt_path().string()) : load_word_db(argv[1]);
    }();

    print_about_expriment_env(word_db);

    auto const tests = std::vector{{
        std::pair{&case01, "REUSE/32BIT"},
        std::pair{&case03, "REUSE/64BIT"},
        std::pair{&case02, "RECREATE/32BIT"},
        std::pair{&case04, "RECREATE/64BIT"},
    }};
    auto test_results = std::vector<std::tuple<sz, sz, double>>(tests.size(), {0, 0, 0.0});
    {
        auto testers = std::vector<std::thread>();
        for (auto i = sz(); i < tests.size(); ++i)
        {
            testers.emplace_back(std::thread(tests[i].first, std::ref(word_db), std::ref(test_results[i])));
        }
        std::ranges::for_each(testers, &std::thread::join);
    }
    for (auto i = sz(); i < tests.size(); ++i)
    {
        spdlog::info("[{}] 충돌 확률 = {}% ({}/{})", tests[i].second, std::get<2>(test_results[i]),
                     std::get<1>(test_results[i]), std::get<0>(test_results[i]));
    }
    return EXIT_SUCCESS;
}
