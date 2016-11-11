/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This is the implementation of the Typeface object. Historically, it has
 * just been SkTypeface, but we are migrating to Minikin. For the time
 * being, that choice is hidden under the USE_MINIKIN compile-time flag.
 */

#include "Typeface.h"

#include <pthread.h>

#include "MinikinSkia.h"
#include "SkTypeface.h"
#include "SkPaint.h"

#include <minikin/FontCollection.h>
#include <minikin/FontFamily.h>
#include <minikin/Layout.h>
#include <utils/Log.h>

namespace android {

// Resolve the 1..9 weight based on base weight and bold flag
static void resolveStyle(Typeface* typeface) {
    int weight = typeface->fBaseWeight / 100;
    if (typeface->fSkiaStyle & SkTypeface::kBold) {
        weight += 3;
    }
    if (weight > 9) {
        weight = 9;
    }
    bool italic = (typeface->fSkiaStyle & SkTypeface::kItalic) != 0;
    typeface->fStyle = minikin::FontStyle(weight, italic);
}

Typeface* gDefaultTypeface = NULL;
pthread_once_t gDefaultTypefaceOnce = PTHREAD_ONCE_INIT;

// This installs a default typeface (from a hardcoded path) that allows
// layouts to work (not crash on null pointer) before the default
// typeface is set. This happens if HWUI is used outside of zygote/app_process.
static minikin::FontCollection *makeFontCollection() {
    std::vector<minikin::FontFamily *>typefaces;
    const char *fns[] = {
        "/system/fonts/Roboto-Regular.ttf",
    };

    minikin::FontFamily *family = new minikin::FontFamily();
    for (size_t i = 0; i < sizeof(fns)/sizeof(fns[0]); i++) {
        const char *fn = fns[i];
        ALOGD("makeFontCollection adding %s", fn);
        sk_sp<SkTypeface> skFace = SkTypeface::MakeFromFile(fn);
        if (skFace != NULL) {
            // TODO: might be a nice optimization to get access to the underlying font
            // data, but would require us opening the file ourselves and passing that
            // to the appropriate Create method of SkTypeface.
            minikin::MinikinFont *font = new MinikinFontSkia(std::move(skFace), NULL, 0, 0);
            family->addFont(font);
            font->Unref();
        } else {
            ALOGE("failed to create font %s", fn);
        }
    }
    typefaces.push_back(family);

    minikin::FontCollection *result = new minikin::FontCollection(typefaces);
    family->Unref();
    return result;
}

static void getDefaultTypefaceOnce() {
  minikin::Layout::init();
    if (gDefaultTypeface == NULL) {
        // We expect the client to set a default typeface, but provide a
        // default so we can make progress before that happens.
        gDefaultTypeface = new Typeface;
        gDefaultTypeface->fFontCollection = makeFontCollection();
        gDefaultTypeface->fSkiaStyle = SkTypeface::kNormal;
        gDefaultTypeface->fBaseWeight = 400;
        resolveStyle(gDefaultTypeface);
    }
}

Typeface* Typeface::resolveDefault(Typeface* src) {
    if (src == NULL) {
        pthread_once(&gDefaultTypefaceOnce, getDefaultTypefaceOnce);
        return gDefaultTypeface;
    } else {
        return src;
    }
}

Typeface* Typeface::createFromTypeface(Typeface* src, SkTypeface::Style style) {
    Typeface* resolvedFace = Typeface::resolveDefault(src);
    Typeface* result = new Typeface;
    if (result != 0) {
        result->fFontCollection = resolvedFace->fFontCollection;
        result->fFontCollection->Ref();
        result->fSkiaStyle = style;
        result->fBaseWeight = resolvedFace->fBaseWeight;
        resolveStyle(result);
    }
    return result;
}

Typeface* Typeface::createWeightAlias(Typeface* src, int weight) {
    Typeface* resolvedFace = Typeface::resolveDefault(src);
    Typeface* result = new Typeface;
    if (result != 0) {
        result->fFontCollection = resolvedFace->fFontCollection;
        result->fFontCollection->Ref();
        result->fSkiaStyle = resolvedFace->fSkiaStyle;
        result->fBaseWeight = weight;
        resolveStyle(result);
    }
    return result;
}

Typeface* Typeface::createFromFamilies(const std::vector<minikin::FontFamily*>& families) {
    Typeface* result = new Typeface;
    result->fFontCollection = new minikin::FontCollection(families);
    if (families.empty()) {
        ALOGW("createFromFamilies creating empty collection");
        result->fSkiaStyle = SkTypeface::kNormal;
    } else {
        const minikin::FontStyle defaultStyle;
        minikin::FontFamily* firstFamily = reinterpret_cast<minikin::FontFamily*>(families[0]);
        minikin::MinikinFont* mf = firstFamily->getClosestMatch(defaultStyle).font;
        if (mf != NULL) {
            SkTypeface* skTypeface = reinterpret_cast<MinikinFontSkia*>(mf)->GetSkTypeface();
            // TODO: probably better to query more precise style from family, will be important
            // when we open up API to access 100..900 weights
            result->fSkiaStyle = skTypeface->style();
        } else {
            result->fSkiaStyle = SkTypeface::kNormal;
        }
    }
    result->fBaseWeight = 400;
    resolveStyle(result);
    return result;
}

void Typeface::unref() {
    fFontCollection->Unref();
    delete this;
}

void Typeface::setDefault(Typeface* face) {
    gDefaultTypeface = face;
}

}
