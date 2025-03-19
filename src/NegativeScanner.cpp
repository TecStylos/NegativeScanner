#include <set>
#include <map>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cmath>

#include "glInit.h"
#include "Window.h"
#include "ImageRenderer.h"
#include "Camera.h"
#include "ThreadPool.h"
#include "ReadWriteMutex.h"

constexpr ns::Color COLOR_GREY  = { 0.5f, 0.5f, 0.5f };
constexpr ns::Color COLOR_RED   = { 1.0f, 0.0f, 0.0f };
constexpr ns::Color COLOR_GREEN = { 0.0f, 1.0f, 0.0f };
constexpr ns::Color COLOR_BLUE  = { 0.0f, 0.0f, 1.0f };

struct ImageID
{
    int x, y;
};

struct ImageExt
{
    ImageID id;
    std::shared_ptr<ns::Image> pImg;
};

bool operator<(const ImageID& iid1, const ImageID& iid2)
{
    if (iid1.x == iid2.x)
        return iid1.y < iid2.y;
    return iid1.x < iid2.x;
}
bool operator<(const ImageID& iid, const ImageExt& ie) { return iid < ie.id; }
bool operator<(const ImageExt& ie, const ImageID& iid) { return ie.id < iid; }
bool operator<(const ImageExt& ie1, const ImageExt& ie2) { return ie1.id < ie2.id; }

struct ImageInfo
{
    bool has_been_adjusted;
    int pos_x, pos_y;
    int width, height;
};

struct Rect
{
    int x, y;
    int w, h;
};

struct DiffScoreThreadData
{
    int min_x, min_y;
    int off_x, off_y;
    std::shared_ptr<std::vector<std::vector<float>>> diff_scores;
};

ns::ThreadPool<DiffScoreThreadData> g_thread_pool(32);

std::string g_capture_dir;

ns::Camera g_camera;

ns::ReadWriteMutex g_image_mtx;
std::set<ImageExt, std::less<>> g_images_base;
ImageExt g_image_current;
ImageExt g_image_overlap;
ImageInfo g_image_overlap_info;

int g_image_current_init_x = 0;
int g_image_current_init_y = 0;
ns::Color g_image_current_border_color = COLOR_GREY;

int g_curr_id_x;
int g_curr_id_y;
bool g_view_image_overlap;
bool g_view_borders;

std::vector<std::vector<ImageInfo>> g_image_info;

std::pair<int, int> get_ids_from_filename(const std::string& filename)
{
    // filename is of format "capture-<x>-<y>.jpg"

    static const std::string PREFIX = "capture-";
    static const std::string SUFFIX = ".jpg";
    // Remove prefix & suffix from filename
    std::string name = filename.substr(PREFIX.size(), filename.size() - PREFIX.size() - SUFFIX.size());

    size_t split_pos = name.find("-");

    std::string x_str = name.substr(0, split_pos);
    std::string y_str = name.substr(split_pos + 1);

    int id_x = std::stoi(x_str);
    int id_y = std::stoi(y_str);

    return { id_x / 10, id_y / 10 }; // Division by 10 only required for test scan. Future scans use indices, not positions
}

std::string get_filename_from_ids(int id_x, int id_y)
{
    std::stringstream ss;
    ss << "capture-";
    ss << std::setfill('0') << std::setw(3) << (id_x * 10);
    ss << "-";
    ss << std::setfill('0') << std::setw(3) << (id_y * 10);
    ss << ".jpg";

    return ss.str();
}

ImageInfo& get_img_info_from_ext(const ImageExt& img_ext)
{
    return g_image_info[img_ext.id.x][img_ext.id.y];
}

ImageExt load_image(int id_x, int id_y)
{
    ImageExt img_ext;
    img_ext.id.x = id_x;
    img_ext.id.y = id_y;

    std::string filepath = g_capture_dir + get_filename_from_ids(id_x, id_y);

    auto reverse_vignette = [](ns::Color c, float u, float v)
    {
        u = (u - 0.5f) * 2.0f;
        v = (v - 0.5f) * 2.0f;
        float m = 1.5f;
        float s = std::sqrt(u*u + v*v) / m + 1.0f - 1.0f / m;
        return ns::Color{ c.r * s, c.g * s, c.b * s };
    };

    img_ext.pImg = std::make_shared<ns::Image>(filepath, reverse_vignette);

    auto& img_info = get_img_info_from_ext(img_ext);
    img_info.width = img_ext.pImg->get_width();
    img_info.height = img_ext.pImg->get_height();

    return img_ext;
}

void init_image_info()
{
    int max_id_x = 0, max_id_y = 0;

    for (auto it = std::filesystem::directory_iterator(g_capture_dir); it != std::filesystem::directory_iterator(); ++it)
    {
        if (!it->is_regular_file())
            break;

        std::string filename = it->path().filename().generic_string();

        auto [id_x, id_y] = get_ids_from_filename(filename);

        if (id_x > max_id_x)
            max_id_x = id_x;
        if (id_y > max_id_y)
            max_id_y = id_y;
    }

    g_curr_id_x = 1;
    g_curr_id_y = 0;

    g_image_info.resize(max_id_x + 1, std::vector<ImageInfo>(max_id_y, ImageInfo{}));

    g_image_info[0][0].has_been_adjusted = true;
}

void render_image(const ImageExt& img_ext, const ImageInfo& img_info, float opacity, bool has_border, ns::Color border_color)
{
    static ns::ImageRenderer s_image_renderer;

    s_image_renderer.render(*img_ext.pImg, img_info.pos_x, img_info.pos_y, g_camera, opacity, has_border, border_color);
}

void render_image(const ImageExt& img_ext, float opacity, bool has_border, ns::Color border_color)
{
    auto& img_info = get_img_info_from_ext(img_ext);
    render_image(img_ext, img_info, opacity, has_border, border_color);
}

Rect get_rect_from_img_info(const ImageInfo& img_info)
{
    Rect rect;

    rect.x = img_info.pos_x;
    rect.y = img_info.pos_y;
    rect.w = img_info.width;
    rect.h = img_info.height;

    return rect;
}

Rect get_overlap_rect(const Rect& rect, const ImageInfo& img_info)
{
    Rect new_rect;
    new_rect.x = std::max(rect.x, img_info.pos_x);
    new_rect.y = std::max(rect.y, img_info.pos_y);
    new_rect.w = std::min(rect.x + rect.w - new_rect.x, img_info.pos_x + img_info.width);
    new_rect.h = std::min(rect.y + rect.h - new_rect.y, img_info.pos_y + img_info.height);

    return new_rect;
}

Rect get_overlap_rect(int off_x, int off_y)
{
    //ns::ReadLock lock(g_image_mtx);

    auto& img_info_curr = get_img_info_from_ext(g_image_current);
    Rect rect = get_rect_from_img_info(img_info_curr);
    rect.x += off_x;
    rect.y += off_y;

    for (auto& image_base : g_images_base)
    {
        auto& img_info_base = get_img_info_from_ext(image_base);
        rect = get_overlap_rect(rect, img_info_base);
    }

    return rect;
}

bool image_overlap_is_outdated()
{
    //ns::ReadLock lock(g_image_mtx);

    Rect r = get_overlap_rect(0, 0);

    return !g_image_overlap.pImg ||
           r.x != g_image_overlap_info.pos_x          || r.y != g_image_overlap_info.pos_y ||
           r.w != g_image_overlap.pImg->get_width() || r.h != g_image_overlap.pImg->get_height();
}

float calculate_diff_rect(int off_x, int off_y, bool update_image_overlap)
{
    //ns::ReadLock lock(g_image_mtx);

    Rect rect_combined = get_overlap_rect(off_x, off_y);

    //if (update_image_overlap)
    //{
    //    g_image_overlap_info.pos_x = rect_combined.x;
    //    g_image_overlap_info.pos_y = rect_combined.y;
    //
    //    g_image_overlap.pImg = std::make_shared<ns::Image>(rect_combined.w, rect_combined.h);
    //}

    auto& img_info_curr = get_img_info_from_ext(g_image_current);
    Rect rect_img_curr = get_rect_from_img_info(img_info_curr);
    rect_img_curr.x += off_x;
    rect_img_curr.y += off_y;
    
    float size_combined = 0.0f;
    float diff_score_combined = 0.0f;

    for (auto& image_base : g_images_base)
    {
        auto& img_info_base = get_img_info_from_ext(image_base);

        Rect rect = get_overlap_rect(rect_img_curr, img_info_base);

        float diff_score = 0.0f;

        for (int y = 0; y < rect.h; ++y)
        {
            for (int x = 0; x < rect.w; ++x)
            {
                int base_x = rect.x - img_info_base.pos_x + x;
                int base_y = rect.y - img_info_base.pos_y + y;
                ns::Color c_base = image_base.pImg->get_pixel(base_x, base_y);
                int curr_x = rect.x - img_info_curr.pos_x - off_x + x;
                int curr_y = rect.y - img_info_curr.pos_y - off_y + y;
                ns::Color c_curr = g_image_current.pImg->get_pixel(curr_x, curr_y);

                float diff = std::abs((c_base.r + c_base.g + c_base.b) - (c_curr.r + c_curr.g + c_curr.b)) / 3.0f;
                
                //if (update_image_overlap)
                //    g_image_overlap.pImg->put_pixel({ diff, diff, diff }, x, y);

                diff_score += diff;
            }
        }

        size_combined += rect.w * rect.h;
        diff_score_combined += diff_score;
    }

    diff_score_combined /= size_combined;

    return diff_score_combined;
}

void update_images()
{
    //ns::WriteLock lock(g_image_mtx);

    if (g_curr_id_x != g_image_current.id.x || g_curr_id_y != g_image_current.id.y)
    {
        int new_x, new_y;
        {
            auto& img_curr_info = get_img_info_from_ext(g_image_current);
            new_x = img_curr_info.pos_x * 2 - g_image_current_init_x;
            new_y = img_curr_info.pos_y * 2 - g_image_current_init_y;
        }

        g_image_current = load_image(g_curr_id_x, g_curr_id_y);
        
        // Predict position if image has never been adjusted
        {
            auto& img_info_curr = get_img_info_from_ext(g_image_current);
            if (!img_info_curr.has_been_adjusted)
            {
                img_info_curr.pos_x = new_x;
                img_info_curr.pos_y = new_y;

                g_image_current_init_x = new_x;
                g_image_current_init_y = new_y;
            }
        }

        // Remove out of bounds bases
        {
            auto& img_info_curr = get_img_info_from_ext(g_image_current);
            Rect rect = get_rect_from_img_info(img_info_curr);

            auto it = g_images_base.begin();
            while (it != g_images_base.end())
            {
                auto curr = it++;

                auto& img_info = get_img_info_from_ext(*curr);
                Rect overlap_rect = get_overlap_rect(rect, img_info);
                if (overlap_rect.w <= 0 || overlap_rect.h <= 0)
                    g_images_base.erase(curr);
            }
        }

        // Load in-bounds bases
        {
            auto& img_info_curr = get_img_info_from_ext(g_image_current);
            Rect rect = get_rect_from_img_info(img_info_curr);

            for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
            {
                for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
                {
                    auto& img_info = g_image_info[id_x][id_y];

                    // Skip untouched images
                    if (!img_info.has_been_adjusted)
                        continue;

                    // Skip already loaded images
                    if (g_images_base.find(ImageID{ id_x, id_y }) != g_images_base.end())
                        continue;

                    // Skip out of bounds images
                    Rect overlap_rect = get_overlap_rect(rect, img_info);
                    if (overlap_rect.w <= 0 || overlap_rect.h <= 0)
                        continue;

                    g_images_base.insert(load_image(id_x, id_y));
                }
            }
        }

        // Load default image base
        if (g_images_base.empty())
            g_images_base.insert(load_image(0, 0));
    }

    if (g_view_image_overlap)
    {
        if (image_overlap_is_outdated())
        {
            float diff_score = calculate_diff_rect(0, 0, true);

            printf("Diff score: %f\n", diff_score);
        }
    }
}

void render_images_base()
{
    //ns::ReadLock lock(g_image_mtx);

    bool is_first = true;

    for (auto& image_base : g_images_base)
    {
        float opacity = 1.0f;//  / (is_first ? 1.0f : g_images_base.size());
        render_image(image_base, opacity, false, {});

        is_first = false;
    }
}

void render_image_current()
{
    //ns::ReadLock lock(g_image_mtx);

    render_image(g_image_current, 0.5f, true, g_image_current_border_color);
}

void render_image_overlap()
{
    //ns::ReadLock lock(g_image_mtx);

    if (g_view_image_overlap)
        render_image(g_image_overlap, g_image_overlap_info, 1.0f, false, {});
}

void render_borders()
{
    //ns::ReadLock lock(g_image_mtx);

    if (g_view_borders)
    {
        for (auto& row : g_image_info)
            for (auto& info : row)
                if (info.has_been_adjusted)
                    render_image(g_image_current, info, 0.0f, true, COLOR_BLUE); // Transparent render, image content is irrelevant
    }
}

void render_func()
{
    update_images();

    render_images_base();
    render_image_current();
    render_image_overlap();
    render_borders();
}

bool move_to_best_diff_score(int test_range)
{
    //ns::ReadLock lock(g_image_mtx);

    printf("Testing for best diff score in range %d...\n", test_range);

    auto diff_scores = std::make_shared<std::vector<std::vector<float>>>(test_range * 2 + 1, std::vector<float>(test_range * 2 + 1, 0.0f));

    for (int off_x = -test_range; off_x <= test_range; ++off_x)
    {
        for (int off_y = -test_range; off_y <= test_range; ++off_y)
        {
            DiffScoreThreadData data;
            data.min_x = -test_range;
            data.min_y = -test_range;
            data.off_x = off_x;
            data.off_y = off_y;
            data.diff_scores = diff_scores;

            auto func = [](const DiffScoreThreadData& data)
            {
                int id_x = data.off_x - data.min_x;
                int id_y = data.off_y - data.min_y;
                (*data.diff_scores)[id_x][id_y] = calculate_diff_rect(data.off_x, data.off_y, false);
            };

            g_thread_pool.push_job({ func, data });
        }
    }

    while (!g_thread_pool.is_idle())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    int best_off_x = 0, best_off_y = 0;
    float best_diff_score = 1.1f;

    for (int off_x = -test_range; off_x <= test_range; ++off_x)
    {
        for (int off_y = -test_range; off_y <= test_range; ++off_y)
        {
            int id_x = off_x + test_range;
            int id_y = off_y + test_range;
            float diff_score = (*diff_scores)[id_x][id_y];

            if (diff_score < best_diff_score)
            {
                best_diff_score = diff_score;
                best_off_x = off_x;
                best_off_y = off_y;
            }
        }
    }

    get_img_info_from_ext(g_image_current).pos_x += best_off_x;
    get_img_info_from_ext(g_image_current).pos_y += best_off_y;
    
    printf("  -> Diff score: %f -- off_x:%d off_y:%d\n", best_diff_score, best_off_x, best_off_y);

    return best_off_x || best_off_y;
}

bool move_to_local_minimum(int test_range, int max_iter)
{
    g_image_current_border_color = COLOR_GREY;

    // Loops 'indefinitely', when max_iter <= 0
    while (--max_iter != 0)
        if (!move_to_best_diff_score(test_range))
            break;

    bool reached_local_minimum = max_iter != 0;

    if (reached_local_minimum)
        g_image_current_border_color = COLOR_GREEN;
    else
        g_image_current_border_color = COLOR_RED;

    return reached_local_minimum;
}

void handle_events(ns::Events& events)
{
    std::optional<std::unique_ptr<const ns::Event>> opt_event;
    while ((opt_event = events.try_pop()).has_value())
    {
        auto pEvent = opt_event.value().get();

        switch (pEvent->get_type())
        {
        case ns::EventType::Mouse:
        {
            auto& event = pEvent->as_type<ns::MouseEvent>();
            
            static float down_x = 0.0f;
            static float down_y = 0.0f;

            switch (event.get_state())
            {
            case ns::MouseEvent::State::Down:
            {
                if (event.get_button() != ns::MouseEvent::Button::Left && event.get_button() != ns::MouseEvent::Button::Right)
                    break;

                down_x = event.get_pos_x();
                down_y = event.get_pos_y();
                break;
            }
            case ns::MouseEvent::State::Up:
            {
                //ns::ReadLock lock(g_image_mtx);

                if (event.get_button() != ns::MouseEvent::Button::Left && event.get_button() != ns::MouseEvent::Button::Right)
                    break;

                float off_x = event.get_pos_x() - down_x;
                float off_y = event.get_pos_y() - down_y;

                auto& img_info_curr = get_img_info_from_ext(g_image_current);
                img_info_curr.pos_x += off_x / g_camera.zoom * 2.0f;
                img_info_curr.pos_y += off_y / g_camera.zoom * 2.0f;
                img_info_curr.has_been_adjusted = true;

                if (event.get_button() == ns::MouseEvent::Button::Left)
                    move_to_local_minimum(1, 64);
            }
            case ns::MouseEvent::State::None: // Should never occur
                break;
            }
            break;
        }
        case ns::EventType::Keyboard:
        {
            auto& event = pEvent->as_type<ns::KeyboardEvent>();

            if (event.get_state() != ns::KeyboardEvent::State::Down)
                break;

            float cam_move_speed = 0.05f;
            float cam_zoom_speed = 1.05f;

            switch (event.get_key())
            {
            case 'w':
                g_camera.y += cam_move_speed / g_camera.zoom;
                break;
            case 's':
                g_camera.y -= cam_move_speed / g_camera.zoom;
                break;
            case 'a':
                g_camera.x += cam_move_speed / g_camera.zoom;
                break;
            case 'd':
                g_camera.x -= cam_move_speed / g_camera.zoom;
                break;
            case 'e':
                g_camera.zoom *= cam_zoom_speed;
                break;
            case 'q':
                g_camera.zoom /= cam_zoom_speed;
                break;
            case 'i':
            {
                //ns::ReadLock lock(g_image_mtx);

                auto& img_info = get_img_info_from_ext(g_image_current);
                img_info.pos_y -= 1;
                img_info.has_been_adjusted = true;
                break;
            }
            case 'k':
            {
                //ns::ReadLock lock(g_image_mtx);

                auto& img_info = get_img_info_from_ext(g_image_current);
                img_info.pos_y += 1;
                img_info.has_been_adjusted = true;
                break;
            }
            case 'j':
            {
                //ns::ReadLock lock(g_image_mtx);

                auto& img_info = get_img_info_from_ext(g_image_current);
                img_info.pos_x -= 1;
                img_info.has_been_adjusted = true;
                break;
            }
            case 'l':
            {
                //ns::ReadLock lock(g_image_mtx);

                auto& img_info = get_img_info_from_ext(g_image_current);
                img_info.pos_x += 1;
                img_info.has_been_adjusted = true;
                break;
            }
            case 'n':
            {
                //ns::ReadLock lock(g_image_mtx);

                auto& img_info = get_img_info_from_ext(g_image_current);
                if (!img_info.has_been_adjusted)
                {
                    printf("Adjust image before jumping to the next\n");
                    break;
                }

                if (++g_curr_id_x >= (int)g_image_info.size())
                {
                    g_curr_id_x = 0;

                    if (++g_curr_id_y >= (int)g_image_info[0].size())
                    {
                        // Last image reached, generate result and store on disk.
                        g_curr_id_x = 1;
                        g_curr_id_y = 0;
                    }
                }

                g_image_current_border_color = COLOR_GREY;

                break;
            }
            case 'p':
            {
                //ns::ReadLock lock(g_image_mtx);

                if (--g_curr_id_x < 0)
                {
                    g_curr_id_x = (int)g_image_info.size() - 1;

                    if (--g_curr_id_y < 0)
                    {
                        g_curr_id_y = (int)g_image_info[0].size() - 1;
                    }
                }

                if (g_curr_id_x == 0 && g_curr_id_y == 0)
                {
                    g_curr_id_x = (int)g_image_info.size() - 1;
                    g_curr_id_y = (int)g_image_info[0].size() - 1;
                }
                break;
            }
            case 'o':
            {
                g_view_image_overlap = !g_view_image_overlap;
                break;
            }
            case 'b':
            {
                g_view_borders = !g_view_borders;
                break;
            }
            case 't':
            {
                move_to_best_diff_score(3);
                break;
            }
            case 'y':
            {
                move_to_local_minimum(3, 32);
                break;
            }
            }
            break;
        }
        default:
        {
            printf("Unhandled event type %d\n", (int)pEvent->get_type());
            break;
        }
        }
    }
}

void main_func(ns::Window* pWindow, ns::Events& events, std::function<bool (void)> is_alive)
{
    pWindow->set_render_func(render_func);

    g_camera.x = 0.0f;
    g_camera.y = 0.0f;
    g_camera.zoom = 0.0005f;

    while (is_alive())
    {
        handle_events(events);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv)
{
    ns::init(&argc, argv);

    if (!ns::is_initialized())
        exit(EXIT_FAILURE);

    if (argc != 2)
    {
        printf("Usage: %s <capture-dir>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    g_capture_dir = argv[1];
    init_image_info();

    ns::Window window("NegativeScanner", 1000, 1000, 460, 20, main_func);

    return 0;
}