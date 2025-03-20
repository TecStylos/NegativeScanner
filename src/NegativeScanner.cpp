#include <set>
#include <map>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <shared_mutex>
#include <cmath>

#include "glInit.h"
#include "Window.h"
#include "ImageRenderer.h"
#include "Camera.h"
#include "ThreadPool.h"
#include "ReadWriteMutex.h"

constexpr ns::Color COLOR_GREY       = { 0.5f, 0.5f, 0.5f, 1.0f };
constexpr ns::Color COLOR_RED        = { 1.0f, 0.0f, 0.0f, 1.0f };
constexpr ns::Color COLOR_GREEN      = { 0.0f, 1.0f, 0.0f, 1.0f };
constexpr ns::Color COLOR_BLUE       = { 0.0f, 0.0f, 1.0f, 1.0f };
constexpr ns::Color COLOR_TURQUOISE  = { 0.2f, 0.5f, 0.3f, 1.0f };
constexpr ns::Color COLOR_LIGHT_BLUE = { 0.4f, 0.4f, 1.0f, 1.0f };
constexpr ns::Color COLOR_ORANGE     = { 1.0f, 0.6f, 0.0f, 1.0f };

constexpr size_t MAX_LOADED_IMAGES = 160;
constexpr int MAX_ITER_TEST_LOCAL_MINIMUM = 16;

struct ImageID
{
    int x, y;
};

bool operator==(const ImageID& iid1, const ImageID& iid2) { return iid1.x == iid2.x && iid1.y == iid2.y; }

struct ImageExt
{
    ImageID id;
    bool is_filtered;
    ns::Image img;
};

struct ImageExtLocked
{
    const ImageExt* p_img_ext;
    std::shared_lock<std::shared_mutex> lock;
public:
    const ImageExt& operator*() const { return *p_img_ext; }
    const ImageExt* operator->() { return p_img_ext; }
public:
    operator bool() { return p_img_ext; }
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
    int pos_x, pos_y;
    int width, height;
    bool has_been_adjusted;
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
    int step_size;
    std::vector<std::vector<float>>* p_diff_scores;
};

ns::ThreadPool<DiffScoreThreadData> g_thread_pool(32);

std::filesystem::path g_capture_dir;

ns::Camera g_camera = { 0.0f, 0.0f, 0.0005f };

std::shared_mutex g_images_mtx;

std::set<ImageExt, std::less<>> g_loaded_images;

std::set<ImageID> g_image_base_ids;
ImageID g_image_current_id;
ImageExt g_image_overlap;
ImageInfo g_image_overlap_info;

bool g_max_image_use_distance_enabled = false;
int g_max_image_use_distance = 0;
bool g_hide_image_current = false;
int g_image_current_init_x = 0;
int g_image_current_init_y = 0;
float g_image_current_opacity = 0.5f;
ns::Color g_image_current_border_color = COLOR_GREY;

int g_test_range = 3;
ImageID g_next_id = { 1, 0 };
bool g_view_image_overlap = false;
bool g_view_borders = false;
bool g_make_images_base_transparent = false;
bool g_select_image_with_mouse = false;
ImageID g_img_id_closest_to_mouse;

std::vector<std::vector<ImageInfo>> g_image_info;

template <typename T>
std::istream& read_bin(std::istream& is, T& data)
{
    return is.read(reinterpret_cast<char*>(&data), sizeof(data));
}

template <typename T>
std::ostream& write_bin(std::ostream& os, const T& data)
{
    return os.write(reinterpret_cast<const char*>(&data), sizeof(data));
}

void load_project_from_file(const std::filesystem::path& filepath)
{
    std::ifstream file(filepath, std::ios::binary);

    if (!file.good())
    {
        printf("Unable to load project from file %s\n", filepath.c_str());
        exit(EXIT_FAILURE);
    }

    printf("Loading project from file %s...\n", filepath.c_str());

    // Load g_capture_dir
    {
        size_t text_size;
        read_bin(file, text_size);
        char text[text_size];
        file.read(text, text_size);
        g_capture_dir = std::string(text, text_size);
    }

    // Load g_camera
    read_bin(file, g_camera);

    // Load g_max_image_use_distance_enabled
    read_bin(file, g_max_image_use_distance_enabled);

    // Load g_max_image_use_distance
    read_bin(file, g_max_image_use_distance);

    // Load g_image_current_opacity
    read_bin(file, g_image_current_opacity);

    // Load g_test_range
    read_bin(file, g_test_range);

    // Load g_next_id
    read_bin(file, g_next_id);

    // Load g_view_image_overlap
    read_bin(file, g_view_image_overlap);

    // Load g_view_borders
    read_bin(file, g_view_borders);

    // Load g_make_images_base_transparent
    read_bin(file, g_make_images_base_transparent);

    // Load g_image_info
    {
        size_t width, height;
        read_bin(file, width);
        read_bin(file, height);

        g_image_info = std::vector<std::vector<ImageInfo>>(width, std::vector<ImageInfo>(height, ImageInfo{}));

        for (int id_x = 0; id_x < (int)width; ++id_x)
        {
            for (int id_y = 0; id_y < (int)height; ++id_y)
            {
                auto& img_info = g_image_info[id_x][id_y];
                read_bin(file, img_info);
            }
        }
    }

    printf("DONE!\n");
}

void save_project_to_file()
{
    std::filesystem::path filepath = g_capture_dir;
    filepath.replace_filename(filepath.filename().generic_string() + ".nsp");

    printf("Project filepath: %s\n", filepath.c_str());

    std::ofstream file(filepath, std::ios::binary);

    if (!file.good())
    {
        printf("Unable to save project to file %s\n", filepath.c_str());
        return;
    }

    printf("Saving project to file %s...\n", filepath.c_str());

    // Save g_capture_dir
    write_bin(file, g_capture_dir.generic_string().size());
    file.write(g_capture_dir.c_str(), g_capture_dir.generic_string().size());

    // Save g_camera
    write_bin(file, g_camera);

    // Save g_max_image_use_distance_enabled
    write_bin(file, g_max_image_use_distance_enabled);

    // Save g_max_image_use_distance
    write_bin(file, g_max_image_use_distance);

    // Save g_image_current_opacity
    write_bin(file, g_image_current_opacity);

    // Save g_test_range
    write_bin(file, g_test_range);

    // Save g_next_id
    write_bin(file, g_next_id);

    // Save g_view_image_overlap
    write_bin(file, g_view_image_overlap);

    // Save g_view_borders
    write_bin(file, g_view_borders);

    // Save g_make_images_base_transparent
    write_bin(file, g_make_images_base_transparent);

    // Save g_image_info
    write_bin(file, g_image_info.size());
    write_bin(file, g_image_info[0].size());

    for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
    {
        for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
        {
            auto& img_info = g_image_info[id_x][id_y];
            write_bin(file, img_info);
        }
    }

    printf("DONE!\n");
}

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

ImageInfo& get_img_info_from_id(const ImageID& img_id)
{
    return g_image_info[img_id.x][img_id.y];
}

ImageInfo& get_img_info_from_ext(const ImageExt& img_ext)
{
    return get_img_info_from_id(img_ext.id);
}

ImageExtLocked get_img_ext_from_id(const ImageID& img_id)
{
    ImageExtLocked iel;
    iel.p_img_ext = nullptr;
    iel.lock = std::shared_lock(g_images_mtx);

    auto it = g_loaded_images.find(img_id);
    if (it != g_loaded_images.end())
        iel.p_img_ext = &*it;
    else
        iel.lock.unlock();

    return iel;
}

int get_img_dist(const ImageInfo& img_info_1, const ImageInfo& img_info_2)
{
    int dx = img_info_1.pos_x - img_info_2.pos_x;
    int dy = img_info_1.pos_y - img_info_2.pos_y;
    return dx*dx + dy*dy;
}

int get_img_dist(const ImageID& img_id_1, const ImageID& img_id_2)
{
    auto& img_info_1 = get_img_info_from_id(img_id_1);
    auto& img_info_2 = get_img_info_from_id(img_id_2);
    return get_img_dist(img_info_1, img_info_2);
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
    new_rect.w = std::min(rect.x + rect.w - new_rect.x, img_info.pos_x + img_info.width - new_rect.x);
    new_rect.h = std::min(rect.y + rect.h - new_rect.y, img_info.pos_y + img_info.height - new_rect.y);

    return new_rect;
}

Rect get_overlap_rect(int off_x, int off_y)
{
    auto& img_info_curr = get_img_info_from_id(g_image_current_id);
    Rect rect = get_rect_from_img_info(img_info_curr);
    rect.x += off_x;
    rect.y += off_y;

    for (auto& img_base_id : g_image_base_ids)
    {
        auto& img_info_base = get_img_info_from_id(img_base_id);
        rect = get_overlap_rect(rect, img_info_base);
    }

    return rect;
}

bool image_overlap_is_outdated()
{
    Rect r = get_overlap_rect(0, 0);

    return r.x != g_image_overlap_info.pos_x      || r.y != g_image_overlap_info.pos_y ||
           r.w != g_image_overlap.img.get_width() || r.h != g_image_overlap.img.get_height();
}

ImageID load_image(int id_x, int id_y, bool make_viewable = true, bool use_filter = true)
{
    // Check if image has already been loaded
    {
        std::unique_lock lock(g_images_mtx);

        auto it = g_loaded_images.find(ImageID{ id_x, id_y });
        if (it != g_loaded_images.end())
        {
            // If same settings applied, return image id
            if (it->is_filtered == use_filter &&
                it->img.is_viewable() == make_viewable)
            {
                printf("Loaded in-memory image %d/%d\n", id_x, id_y);
                return { id_x, id_y };
            }
            else // Otherwise remove loaded image and continue loading the correct one
            {
                g_loaded_images.erase(it);
                g_image_base_ids.erase({ id_x, id_y });
                printf("Removed image %d/%d with wrong settings\n", id_x, id_y);
            }
        }
    }

    printf("Loading image %d/%d from disk...\n", id_x, id_y);

    ImageExt img_ext;
    img_ext.id.x = id_x;
    img_ext.id.y = id_y;
    img_ext.is_filtered = use_filter;

    std::string filepath = g_capture_dir / get_filename_from_ids(id_x, id_y);

    auto reverse_vignette = [](ns::Color c, float u, float v)
    {
        u = (u - 0.5f) * 2.0f;
        v = (v - 0.5f) * 2.0f;
        float m = 1.5f;
        float s = std::sqrt(u*u + v*v) / m + 1.0f - 1.0f / m;
        c.r *= s;
        c.g *= s;
        c.b *= s;
        c.a = c.r * c.r + c.g * c.g + c.b * c.b;
        return c;
    };

    if (use_filter)
        img_ext.img = ns::Image::load_from_file(filepath, make_viewable, reverse_vignette);
    else
        img_ext.img = ns::Image::load_from_file(filepath, make_viewable);

    auto& img_info = get_img_info_from_ext(img_ext);
    img_info.width = img_ext.img.get_width();
    img_info.height = img_ext.img.get_height();


    std::unique_lock lock(g_images_mtx);

    if (g_loaded_images.size() >= MAX_LOADED_IMAGES)
    {
        printf("Image buffer full, searching best candidate to remove...\n");

        int furthest_dist = 0;
        bool furthest_is_in_use = true;
        ImageID furthest_img_id = { 0, 0 };

        for (auto& img_ext : g_loaded_images)
        {
            int dist = get_img_dist(g_image_current_id, img_ext.id);

            // Prefer images not in use
            bool is_in_use = g_image_base_ids.find(img_ext.id) != g_image_base_ids.end();

            if ((!is_in_use && furthest_is_in_use) ||
                (dist > furthest_dist))
            {
                furthest_dist = dist;
                furthest_is_in_use = is_in_use;
                furthest_img_id = img_ext.id;
            }
        }

        g_loaded_images.erase(g_loaded_images.find(furthest_img_id));
        g_image_base_ids.erase(furthest_img_id);

        printf("Removed image %d/%d from image buffer (in use?: %d)\n", furthest_img_id.x, furthest_img_id.y, (int)furthest_is_in_use);
    }

    g_loaded_images.insert(std::move(img_ext));

    return { id_x, id_y };
}

void render_final_image()
{
    printf("Retrieving final image dimensions...\n");
    int min_x = 0, min_y = 0;
    int max_x = 0, max_y = 0;
    for (auto& row : g_image_info)
    {
        for (auto& img_info : row)
        {
            // Skip unplaced images
            if (!img_info.has_been_adjusted)
                continue;

            if (img_info.pos_x < min_x)
                min_x = img_info.pos_x;
            if (img_info.pos_y < min_y)
                min_y = img_info.pos_y;
            
            if (img_info.pos_x + img_info.width > max_x)
                max_x = img_info.pos_x + img_info.width;
            if (img_info.pos_y + img_info.height > max_y)
                max_y = img_info.pos_y + img_info.height;
        }
    }

    int width = max_x - min_x;
    int height = max_y - min_y;

    printf("Allocating image memory...\n");
    ns::Image img(width, height, false);


    printf("Combining images...\n");
    for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
    {
        for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
        {
            printf("\e[0E[%lu/%lu]", id_x * g_image_info[0].size() + id_y, g_image_info.size() * g_image_info[0].size());
            fflush(stdout);

            auto& sub_img_info = g_image_info[id_x][id_y];

            if (!sub_img_info.has_been_adjusted)
                continue;

            auto sub_img_id = load_image(id_x, id_y, false, false);
            auto sub_img = get_img_ext_from_id(sub_img_id);

            if (!sub_img)
            {
                printf("Unable to get image %d/%d\n", id_x, id_y);
                continue;
            }

            for (int x = 0; x < sub_img_info.width; ++x)
            {
                for (int y = 0; y < sub_img_info.height; ++y)
                {
                    int img_x = sub_img_info.pos_x + x - min_x;
                    int img_y = sub_img_info.pos_y + y - min_y;

                    auto sub_c = sub_img->img.get_pixel(x, y);
                    auto img_c = img.get_pixel(img_x, img_y);

                    img_c.r += sub_c.r;
                    img_c.g += sub_c.g;
                    img_c.b += sub_c.b;
                    img_c.a += 1.0f;

                    img.put_pixel(img_c, img_x, img_y);
                }
            }
        }
    }

    printf("Cleaning up image...\n");
    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
        {
            ns::Color c = img.get_pixel(x, y);

            c.r /= c.a;
            c.g /= c.a;
            c.b /= c.a;

            img.put_pixel(c, x, y);
        }
    }

    std::filesystem::path filepath = g_capture_dir;
    filepath.replace_filename(filepath.filename().generic_string() + ".jpg");

    printf("Saving image to file %s...\n", filepath.c_str());
    if (!img.save_to_file(filepath))
        printf("Could not save image\n");
    else
        printf("DONE!\n");
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

    g_image_info.resize(max_id_x + 1, std::vector<ImageInfo>(max_id_y + 1, ImageInfo{}));

    g_image_info[0][0].has_been_adjusted = true;
}

void render_image(const ImageExt& img_ext, const ImageInfo& img_info, float opacity, bool has_border, ns::Color border_color)
{
    static ns::ImageRenderer s_image_renderer;

    s_image_renderer.render(img_ext.img, img_info.pos_x, img_info.pos_y, g_camera, opacity, has_border, border_color);
}

void render_image(const ImageExt& img_ext, float opacity, bool has_border, ns::Color border_color)
{
    auto& img_info = get_img_info_from_ext(img_ext);
    render_image(img_ext, img_info, opacity, has_border, border_color);
}

void render_image(const ImageID& img_id, float opacity, bool has_border, ns::Color border_color)
{
    auto img_ext = get_img_ext_from_id(img_id);
    if (!img_ext)
    {
        printf("Cannot render image %d/%d\n", img_id.x, img_id.y);
        return;
    }

    render_image(*img_ext, opacity, has_border, border_color);
}

float calculate_diff_rect(int off_x, int off_y, int step_size, bool update_image_overlap)
{
    (void)update_image_overlap;
    //Rect rect_combined = get_overlap_rect(off_x, off_y);

    //if (update_image_overlap)
    //{
    //    g_image_overlap_info.pos_x = rect_combined.x;
    //    g_image_overlap_info.pos_y = rect_combined.y;
    //
    //    g_image_overlap.img = std::make_shared<ns::Image>(rect_combined.w, rect_combined.h);
    //}

    auto img_curr = get_img_ext_from_id(g_image_current_id);
    if (!img_curr)
    {
        printf("Cannot calculate diff_rect for image %d/%d\n", g_image_current_id.x, g_image_current_id.y);
        return 0.0f;
    }

    auto& img_curr_info = get_img_info_from_id(g_image_current_id);
    Rect img_curr_rect = get_rect_from_img_info(img_curr_info);
    img_curr_rect.x += off_x;
    img_curr_rect.y += off_y;
    
    float size_combined = 0.0f;
    float diff_score_combined = 0.0f;

    for (auto& img_base_id : g_image_base_ids)
    {
        auto img_base = get_img_ext_from_id(img_base_id);
        if (!img_base)
        {
            printf("Cannot calculate partial diff_rect for image %d/%d\n", img_base_id.x, img_base_id.y);
            continue;
        }

        auto& img_info_base = get_img_info_from_id(img_base_id);

        Rect rect = get_overlap_rect(img_curr_rect, img_info_base);

        float diff_score = 0.0f;

        for (int y = 0; y < rect.h; y += step_size)
        {
            for (int x = 0; x < rect.w; x += step_size)
            {
                int base_x = rect.x - img_info_base.pos_x + x;
                int base_y = rect.y - img_info_base.pos_y + y;
                ns::Color c_base = img_base->img.get_pixel(base_x, base_y);
                int curr_x = rect.x - img_curr_info.pos_x - off_x + x;
                int curr_y = rect.y - img_curr_info.pos_y - off_y + y;
                ns::Color c_curr = img_curr->img.get_pixel(curr_x, curr_y);

                //float diff = std::abs((c_base.r + c_base.g + c_base.b) - (c_curr.r + c_curr.g + c_curr.b)) / 3.0f;
                float diff = std::abs(c_base.a - c_curr.a);

                //if (update_image_overlap)
                //    g_image_overlap.img.put_pixel({ diff, diff, diff }, x, y);

                diff_score += diff;
            }
        }

        size_combined += rect.w * rect.h / step_size / step_size;
        diff_score_combined += diff_score;
    }

    diff_score_combined /= size_combined;

    return diff_score_combined;
}

void update_images()
{
    ns::Image::delete_pending_tex_objs();

    if (g_next_id.x != g_image_current_id.x || g_next_id.y != g_image_current_id.y)
    {
        int new_x, new_y;
        {
            auto& img_curr_info = get_img_info_from_id(g_image_current_id);
            new_x = img_curr_info.pos_x;
            new_y = img_curr_info.pos_y;
        }

        g_image_current_id = load_image(g_next_id.x, g_next_id.y);

        // Predict position if image has never been adjusted
        {
            auto& img_info_curr = get_img_info_from_id(g_image_current_id);
            if (!img_info_curr.has_been_adjusted)
            {
                img_info_curr.pos_x = new_x;
                img_info_curr.pos_y = new_y;

                g_image_current_init_x = new_x;
                g_image_current_init_y = new_y;
            }
        }

        g_image_current_border_color = COLOR_GREY;
    }

    static int prev_x = -1;
    static int prev_y = -1;
    auto& img_info_curr = get_img_info_from_id(g_image_current_id);
    Rect rect_curr = get_rect_from_img_info(img_info_curr);

    if (prev_x != img_info_curr.pos_x || prev_y != img_info_curr.pos_y)
    {
        prev_x = img_info_curr.pos_x;
        prev_y = img_info_curr.pos_y;

        // Remove out of bounds bases
        auto it = g_image_base_ids.begin();
        while (it != g_image_base_ids.end())
        {
            auto curr = it++;

            if (*curr == g_image_current_id)
            {
                g_image_base_ids.erase(curr);
                continue;
            }

            if (g_max_image_use_distance_enabled)
            {
                if (get_img_dist(g_image_current_id, *curr) > g_max_image_use_distance)
                    g_image_base_ids.erase(curr);
            }
            else
            {
                auto& img_info = get_img_info_from_id(*curr);
                Rect overlap_rect = get_overlap_rect(rect_curr, img_info);
                if (overlap_rect.w <= 0 || overlap_rect.h <= 0)
                    g_image_base_ids.erase(curr);
            }
        }

        // Load in-bounds bases
        for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
        {
            for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
            {
                auto& img_info = g_image_info[id_x][id_y];

                // Skip current image
                if (g_image_current_id == ImageID{ id_x, id_y })
                    continue;

                // Skip untouched images
                if (!img_info.has_been_adjusted)
                    continue;

                // Skip images already in use
                if (g_image_base_ids.find(ImageID{ id_x, id_y }) != g_image_base_ids.end())
                    continue;

                // Skip out of bounds images
                if (g_max_image_use_distance_enabled)
                {
                    if (get_img_dist(g_image_current_id, { id_x, id_y }) > g_max_image_use_distance)
                        continue;
                }
                else
                {
                    Rect overlap_rect = get_overlap_rect(rect_curr, img_info);
                    if (overlap_rect.w <= 0 || overlap_rect.h <= 0)
                        continue;
                }

                g_image_base_ids.insert(load_image(id_x, id_y));
            }
        }

        // Load default image base
        if (g_image_base_ids.empty())
            g_image_base_ids.insert(load_image(0, 0));
    }

    if (g_view_image_overlap)
    {
        if (image_overlap_is_outdated())
        {
            float diff_score = calculate_diff_rect(0, 0, true, 1);

            printf("Diff score: %f\n", diff_score);
        }
    }
}

void render_images_base()
{
    for (auto& img_base_id : g_image_base_ids)
    {
        float opacity = 1.0f / ((!g_make_images_base_transparent) ? 1.0f : g_image_base_ids.size());
        render_image(img_base_id, opacity, false, {});
    }
}

void render_image_current()
{
    if (!g_select_image_with_mouse && !g_hide_image_current)
        render_image(g_image_current_id, g_image_current_opacity, true, g_image_current_border_color);
}

void render_image_overlap()
{
    if (g_view_image_overlap && !g_select_image_with_mouse)
        render_image(g_image_overlap, g_image_overlap_info, 1.0f, false, {});
}

void render_borders()
{
    if (g_view_borders)
    {
        for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
        {
            for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
            {
                auto& info = get_img_info_from_id({ id_x, id_y });
                if (info.has_been_adjusted)
                {
                    std::shared_lock lock(g_images_mtx);

                    ns::Color color = COLOR_BLUE;

                    // is loaded
                    if (g_loaded_images.find(ImageID{ id_x, id_y }) != g_loaded_images.end())
                    {
                        // is in use
                        if (g_image_base_ids.find(ImageID{ id_x, id_y }) != g_image_base_ids.end())
                            color = COLOR_TURQUOISE;
                        else
                            color = COLOR_LIGHT_BLUE;
                    }

                    auto img_ext = get_img_ext_from_id(g_image_current_id);
                    if (!img_ext)
                    {
                        printf("Cannot render image %d/%d\n", g_image_current_id.x, g_image_current_id.y);
                        continue;
                    }

                    render_image(*img_ext, info, 0.0f, true, color); // Transparent render, image content is irrelevant
                }
            }
        }
    }
}

void render_image_mouse_select()
{
    if (g_select_image_with_mouse)
    {
        bool is_not_selectable = g_img_id_closest_to_mouse == ImageID{ 0, 0 };

        auto img_ext = get_img_ext_from_id(g_image_current_id);
        if (!img_ext)
        {
            printf("Cannot render image %d/%d\n", g_image_current_id.x, g_image_current_id.y);
            return;
        }

        render_image(*img_ext, g_image_info[g_img_id_closest_to_mouse.x][g_img_id_closest_to_mouse.y], 0.0f, true, is_not_selectable ? COLOR_RED : COLOR_ORANGE);
    }
}

void render_func()
{
    update_images();

    render_images_base();
    render_image_current();
    render_image_overlap();
    render_borders();
    render_image_mouse_select();
}

bool move_to_best_diff_score(int test_range, int step_size)
{
    printf("Testing for best diff score in range %d...\n", test_range);

    static std::vector<std::vector<float>> diff_scores(test_range * 2 + 1, std::vector<float>(test_range * 2 + 1, 0.0f));

    DiffScoreThreadData diff_score_data;
    diff_score_data.min_x = -test_range;
    diff_score_data.min_y = -test_range;
    diff_score_data.step_size = step_size;
    diff_score_data.p_diff_scores = &diff_scores;

    auto diff_score_func = [](const DiffScoreThreadData& data)
    {
        int id_x = data.off_x - data.min_x;
        int id_y = data.off_y - data.min_y;
        (*data.p_diff_scores)[id_x][id_y] = calculate_diff_rect(data.off_x, data.off_y, data.step_size, false);
    };

    for (int off_x = -test_range; off_x <= test_range; ++off_x)
    {
        for (int off_y = -test_range; off_y <= test_range; ++off_y)
        {
            diff_score_data.off_x = off_x;
            diff_score_data.off_y = off_y;

            g_thread_pool.push_job({ diff_score_func, diff_score_data });
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
            float diff_score = diff_scores[id_x][id_y];

            if (diff_score < best_diff_score)
            {
                best_diff_score = diff_score;
                best_off_x = off_x;
                best_off_y = off_y;
            }
        }
    }

    get_img_info_from_id(g_image_current_id).pos_x += best_off_x;
    get_img_info_from_id(g_image_current_id).pos_y += best_off_y;
    
    printf("  -> Diff score: %f -- off_x:%d off_y:%d\n", best_diff_score, best_off_x, best_off_y);

    return best_off_x || best_off_y;
}

bool move_to_local_minimum(int test_range, int max_iter, int step_size)
{
    g_image_current_border_color = COLOR_GREY;

    // Loops 'indefinitely', when max_iter <= 0
    while (--max_iter != 0)
        if (!move_to_best_diff_score(test_range, step_size))
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
        case ns::EventType::MouseMove:
        {
            auto& event = pEvent->as_type<ns::MouseMoveEvent>();

            if (g_select_image_with_mouse)
            {
                int x = (event.get_pos_x() - 0.5f) / g_camera.zoom * 2.0f - g_camera.x;
                int y = (event.get_pos_y() - 0.5f) / g_camera.zoom * 2.0f - g_camera.y;

                int dist_closest = std::numeric_limits<int>::max();

                for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
                {
                    for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
                    {
                        auto& img_base_info = g_image_info[id_x][id_y];

                        if (!img_base_info.has_been_adjusted)
                            continue;

                        Rect rect = get_rect_from_img_info(img_base_info);

                        int dx = rect.x + rect.w / 2 - x;
                        int dy = rect.y + rect.h / 2 - y;

                        int dist = dx*dx + dy*dy;

                        if (dist < dist_closest)
                        {
                            dist_closest = dist;
                            g_img_id_closest_to_mouse = { id_x, id_y };
                        }
                    }
                }
            }
            break;
        }
        case ns::EventType::MouseButton:
        {
            auto& event = pEvent->as_type<ns::MouseButtonEvent>();
            
            static float down_x = 0.0f;
            static float down_y = 0.0f;

            switch (event.get_state())
            {
            case ns::MouseButtonEvent::State::Down:
            {
                g_image_current_border_color = COLOR_GREY;

                if (event.get_button() == ns::MouseButtonEvent::Button::Middle)
                {
                    g_img_id_closest_to_mouse = g_image_current_id;
                    g_select_image_with_mouse = true;
                }

                down_x = event.get_pos_x();
                down_y = event.get_pos_y();
                break;
            }
            case ns::MouseButtonEvent::State::Up:
            {
                switch (event.get_button())
                {
                case ns::MouseButtonEvent::Button::Left:
                case ns::MouseButtonEvent::Button::Right:
                {
                    if (event.get_button() != ns::MouseButtonEvent::Button::Left && event.get_button() != ns::MouseButtonEvent::Button::Right)
                        break;
    
                    float off_x = event.get_pos_x() - down_x;
                    float off_y = event.get_pos_y() - down_y;
    
                    auto& img_info_curr = get_img_info_from_id(g_image_current_id);
                    img_info_curr.pos_x += off_x / g_camera.zoom * 2.0f;
                    img_info_curr.pos_y += off_y / g_camera.zoom * 2.0f;
                    img_info_curr.has_been_adjusted = true;
    
                    if (event.get_button() == ns::MouseButtonEvent::Button::Left)
                    {
                        if (move_to_local_minimum(g_test_range, MAX_ITER_TEST_LOCAL_MINIMUM, 2))
                            move_to_local_minimum(g_test_range, MAX_ITER_TEST_LOCAL_MINIMUM, 1);
                    }

                    break;
                }
                case ns::MouseButtonEvent::Button::Middle:
                {
                    g_select_image_with_mouse = false;

                    // Skip default
                    if (g_img_id_closest_to_mouse == ImageID{ 0, 0 })
                        break;

                    g_next_id = g_img_id_closest_to_mouse;

                    break;
                }
                case ns::MouseButtonEvent::Button::None: // Should never occur
                    break;
                }

            }
            case ns::MouseButtonEvent::State::None: // Should never occur
                break;
            }
            break;
        }
        case ns::EventType::Keyboard:
        {
            auto& event = pEvent->as_type<ns::KeyboardEvent>();
            
            float cam_move_speed = 0.05f;
            float cam_zoom_speed = 1.05f;

            switch (event.get_state())
            {
            case ns::KeyboardEvent::State::Down:
            {
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
                    auto& img_info = get_img_info_from_id(g_image_current_id);
                    img_info.pos_y -= 1;
                    img_info.has_been_adjusted = true;
                    break;
                }
                case 'k':
                {
                    auto& img_info = get_img_info_from_id(g_image_current_id);
                    img_info.pos_y += 1;
                    img_info.has_been_adjusted = true;
                    g_image_current_border_color = COLOR_GREY;
                    break;
                }
                case 'j':
                {
                    auto& img_info = get_img_info_from_id(g_image_current_id);
                    img_info.pos_x -= 1;
                    img_info.has_been_adjusted = true;
                    g_image_current_border_color = COLOR_GREY;
                    break;
                }
                case 'l':
                {
                    auto& img_info = get_img_info_from_id(g_image_current_id);
                    img_info.pos_x += 1;
                    img_info.has_been_adjusted = true;
                    g_image_current_border_color = COLOR_GREY;
                    break;
                }
                case 'n':
                {
                    auto& img_info = get_img_info_from_id(g_image_current_id);
                    if (!img_info.has_been_adjusted)
                    {
                        printf("Adjust image before jumping to the next\n");
                        break;
                    }

                    if (++g_next_id.x >= (int)g_image_info.size())
                    {
                        g_next_id.x = 0;

                        if (++g_next_id.y >= (int)g_image_info[0].size())
                        {
                            // Last image reached, generate result and store on disk.
                            g_next_id = { 1, 0 };
                        }
                    }

                    break;
                }
                case 'p':
                {
                    if (--g_next_id.x < 0)
                    {
                        g_next_id.x = (int)g_image_info.size() - 1;

                        if (--g_next_id.y < 0)
                        {
                            g_next_id.y = (int)g_image_info[0].size() - 1;
                        }
                    }

                    if (g_next_id.x == 0 && g_next_id.y == 0)
                    {
                        g_next_id.x = (int)g_image_info.size() - 1;
                        g_next_id.y = (int)g_image_info[0].size() - 1;
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
                    g_make_images_base_transparent = !g_make_images_base_transparent;
                    break;
                }
                case 'y':
                {
                    move_to_local_minimum(g_test_range, MAX_ITER_TEST_LOCAL_MINIMUM, 1);
                    break;
                }
                case 'f':
                {
                    save_project_to_file();
                    break;
                }
                case 'r':
                {
                    render_final_image();
                    break;
                }
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                {
                    g_test_range = event.get_key() - '0';
                    printf("Set test_range to %d\n", g_test_range);
                    break;
                }
                case ',':
                {
                    if (g_image_current_opacity < 0.1f)
                    {
                        printf("Already at minimum opacity\n");
                        break;
                    }
                    g_image_current_opacity -= 0.1f;
                    printf("Decreased opacity to %f\n", g_image_current_opacity);
                    break;
                }
                case '.':
                {
                    if (g_image_current_opacity > 0.9f)
                    {
                        printf("Already at maximum opacity\n");
                        break;
                    }
                    g_image_current_opacity += 0.1f;
                    printf("Increased opacity to %f\n", g_image_current_opacity);
                    break;
                }
                case ' ':
                {
                    g_hide_image_current = true;
                    break;
                }
                case '[':
                {
                    printf("Disabled max_image_use_distance\n");
                    g_max_image_use_distance_enabled = false;
                    break;
                }
                case ']':
                {
                    g_max_image_use_distance = 0;

                    Rect rect_curr = get_rect_from_img_info(get_img_info_from_id(g_image_current_id));

                    for (int id_x = 0; id_x < (int)g_image_info.size(); ++id_x)
                    {
                        for (int id_y = 0; id_y < (int)g_image_info[0].size(); ++id_y)
                        {
                            Rect rect_overlap = get_overlap_rect(rect_curr, get_img_info_from_id({ id_x, id_y }));

                            if (rect_overlap.w <= 0 || rect_overlap.h <= 0)
                                continue;

                            int dist = get_img_dist(g_image_current_id, { id_x, id_y });
                            if (dist > g_max_image_use_distance)
                                g_max_image_use_distance = dist;
                        }
                    }

                    g_max_image_use_distance /= 2;
                    g_max_image_use_distance_enabled = true;

                    printf("Enabled max_image_use_distance [%d]\n", (int)std::sqrt(g_max_image_use_distance));
                    break;
                }
                }
                break;
            }
            case ns::KeyboardEvent::State::Up:
            {
                switch (event.get_key())
                {
                case ' ':
                {
                    g_hide_image_current = false;
                    break;
                }
                }
                break;
            }
            case ns::KeyboardEvent::State::None: // Should never occur
            {
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
        printf("Usage: %s <capture-dir-or-project-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (std::filesystem::is_regular_file(argv[1]))
    {
        load_project_from_file(argv[1]);
    }
    else if (std::filesystem::is_directory(argv[1]))
    {
        g_capture_dir = argv[1];
        if (!g_capture_dir.has_filename())
        g_capture_dir = g_capture_dir.parent_path();
        init_image_info();
    }
    else
    {
        printf("Provided path '%s' is not a project file or directory path\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    ns::Window window("NegativeScanner", 1000, 1000, 460, 20, main_func);

    return 0;
}