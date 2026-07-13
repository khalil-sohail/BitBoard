#include "app/app_eval_features.hpp"

#include "eval/evaluation_trace.hpp"
#include "tuning/compiled_profile_identity.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[c >> 4] << hex[c & 15];
                } else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

void writeTrace(const std::string& fen, Color side, const Eval::EvaluationFeatureTrace& trace) {
    constexpr auto identity = Tuning::compiledProfileIdentity();
    std::cout << "{\"schemaVersion\":" << Eval::EVALUATION_FEATURE_SCHEMA_VERSION
              << ",\"featureModelVersion\":" << jsonString(Eval::EVALUATION_FEATURE_MODEL_VERSION)
              << ",\"fen\":" << jsonString(fen)
              << ",\"sideToMove\":\"" << (side == Color::White ? "white" : "black") << "\""
              << ",\"profileId\":" << jsonString(std::string(identity.profileId))
              << ",\"profileHash\":" << jsonString(std::string(identity.canonicalHash))
              << ",\"profileSchemaVersion\":" << identity.schemaVersion
              << ",\"modelVersion\":" << jsonString(std::string(identity.modelVersion))
              << ",\"phase\":{\"current\":" << trace.phase << ",\"clamped\":" << trace.clampedPhase << ",\"maximum\":24}"
              << ",\"scores\":{\"baseMiddlegameWhiteCp\":" << trace.baseMiddlegameScore
              << ",\"baseEndgameWhiteCp\":" << trace.baseEndgameScore
              << ",\"middlegameWhiteCp\":" << trace.middlegameScore
              << ",\"endgameWhiteCp\":" << trace.endgameScore
              << ",\"taperedWhiteCp\":" << trace.taperedScore
              << ",\"noPawnScaledWhiteCp\":" << trace.noPawnScaledScore
              << ",\"lowMaterialScale\":" << trace.lowMaterialScale
              << ",\"finalWhiteCp\":" << trace.finalScore
              << ",\"finalFromSideToMoveCp\":" << trace.sideToMoveScore
              << ",\"insufficientMaterialDraw\":" << (trace.insufficientMaterialDraw ? "true" : "false") << "}"
              << ",\"features\":{";
    bool firstFeature = true;
    for (const auto& [name, feature] : trace.features) {
        if (!firstFeature) std::cout << ',';
        firstFeature = false;
        std::cout << jsonString(name) << ":{\"classification\":" << jsonString(feature.classification)
                  << ",\"coefficients\":{";
        bool firstCoefficient = true;
        for (const auto& [index, coefficient] : feature.coefficients) {
            if (coefficient == 0) continue;
            if (!firstCoefficient) std::cout << ',';
            firstCoefficient = false;
            std::cout << jsonString(index) << ':' << coefficient;
        }
        std::cout << "},\"middlegameContribution\":" << feature.middlegameContribution
                  << ",\"endgameContribution\":" << feature.endgameContribution << '}';
    }
    std::cout << "},\"weightedTerms\":[";
    for (size_t i = 0; i < trace.terms.size(); ++i) {
        if (i) std::cout << ',';
        const auto& term = trace.terms[i];
        std::cout << "{\"name\":" << jsonString(term.name) << ",\"middlegame\":" << term.middlegame
                  << ",\"endgame\":" << term.endgame << '}';
    }
    std::cout << "]}\n";
}

} // namespace

namespace AppEvalFeatures {

int run(Board& board) {
    std::string fen;
    size_t lineNumber = 0;
    while (std::getline(std::cin, fen)) {
        ++lineNumber;
        if (!fen.empty() && fen.back() == '\r') fen.pop_back();
        if (fen.empty()) continue;
        if (!board.loadFEN(fen) || board.getBitboard(Color::White, PieceType::King) == 0 ||
            board.getBitboard(Color::Black, PieceType::King) == 0) {
            std::cerr << "eval-features: line " << lineNumber << ": invalid FEN\n";
            continue;
        }
        const auto trace = board.traceEvaluation();
        if (trace.finalScore != board.computeStaticEvaluation() || trace.finalScore != board.evaluate()) {
            std::cerr << "eval-features: line " << lineNumber << ": internal reconstruction mismatch\n";
            return 2;
        }
        writeTrace(fen, board.sideToMove(), trace);
    }
    if (!std::cin.eof() && std::cin.fail()) {
        std::cerr << "eval-features: failed while reading stdin\n";
        return 2;
    }
    return 0;
}

} // namespace AppEvalFeatures
