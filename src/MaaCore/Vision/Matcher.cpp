#include "Matcher.h"

#include "Utils/NoWarningCV.h"

#include "Config/TaskData.h"
#include "Config/TemplResource.h"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"

using namespace asst;

Matcher::ResultOpt Matcher::analyze() const
{
    const auto& [matched, templ, templ_name] = preproc_and_match(make_roi(m_image, m_roi), m_params);

    if (matched.empty()) {
        return std::nullopt;
    }

    double min_val = 0.0, max_val = 0.0;
    cv::Point min_loc, max_loc;
    cv::minMaxLoc(matched, &min_val, &max_val, &min_loc, &max_loc);

    Rect rect(max_loc.x + m_roi.x, max_loc.y + m_roi.y, templ.cols, templ.rows);
    if (std::isnan(max_val) || std::isinf(max_val)) {
        max_val = 0;
    }
    if (m_log_tracing && max_val > m_params.templ_thres * 0.7) { // 得分太低的肯定不对，没必要打印
        Log.trace("match_templ |", templ_name, "score:", max_val, "rect:", rect, "roi:", m_roi);
    }

    if (max_val < m_params.templ_thres) {
        return std::nullopt;
    }

    // FIXME: 老接口太难重构了，先弄个这玩意兼容下，后续慢慢全删掉
    m_result.rect = rect;
    m_result.score = max_val;
    return m_result;
}

Matcher::RawResult Matcher::preproc_and_match(const cv::Mat& image, const MatcherConfig::Params& params)
{
    cv::Mat templ;
    std::string templ_name;

    if (std::holds_alternative<std::string>(params.templ)) {
        templ_name = std::get<std::string>(params.templ);
        templ = TemplResource::get_instance().get_templ(templ_name);
    }
    else if (std::holds_alternative<cv::Mat>(params.templ)) {
        templ = std::get<cv::Mat>(params.templ);
    }
    else {
        Log.error("templ is none");
    }

    if (templ.empty()) {
        Log.error("templ is empty!", templ_name);
#ifdef ASST_DEBUG
        throw std::runtime_error("templ is empty: " + templ_name);
#else
        return {};
#endif
    }

    if (templ.cols > image.cols || templ.rows > image.rows) {
        Log.error("templ size is too large", templ_name, "image size:", image.cols, image.rows,
                  "templ size:", templ.cols, templ.rows);
        return {};
    }

    cv::Mat matched;
    if (params.mask_range.first == 0 && params.mask_range.second == 0) {
        cv::matchTemplate(image, templ, matched, cv::TM_CCOEFF_NORMED);
    }
    else {
        cv::Mat mask;
        cv::cvtColor(params.mask_with_src ? image : templ, mask, cv::COLOR_BGR2GRAY);
        cv::inRange(mask, params.mask_range.first, params.mask_range.second, mask);
        if (params.mask_with_close) {
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
            cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
        }
        cv::matchTemplate(image, templ, matched, cv::TM_CCOEFF_NORMED, mask);
    }

    return RawResult { .matched = matched, .templ = templ, .templ_name = templ_name };
}
