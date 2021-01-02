#include "macros.h"
// reading input data (nsf/folder), parsing and storing it

/** \brief
 *  Reads the nsf, puts it straight into chunks, does not collect references or anything.
 *  Collects relatives. Likely slightly deprecate.
 *
 * \param elist ENTRY*                  list of entries
 * \param chunk_border_base int         chunk count of the base nsf
 * \param chunks unsigned char**        chunk array, base level gets put straight into them
 * \param chunk_border_texture int*     index of the last texture chunk
 * \param entry_count int*              entry count
 * \param nsf FILE*                     nsf file
 * \param gool_table unsigned int*      gool table gets created, contains GOOL entries associated with the type/index they are on
 * \return void
 */
void build_read_nsf(ENTRY *elist, int chunk_border_base, unsigned char **chunks, int *chunk_border_texture, int *entry_count, FILE *nsf, unsigned int *gool_table) {
    int i, j , offset;
    unsigned char chunk[CHUNKSIZE];

    for (i = 0; i < chunk_border_base; i++) {
        fread(chunk, CHUNKSIZE, sizeof(unsigned char), nsf);
        chunks[*chunk_border_texture] = (unsigned char*) calloc(CHUNKSIZE, sizeof(unsigned char));
        memcpy(chunks[*chunk_border_texture], chunk, CHUNKSIZE);
        (*chunk_border_texture)++;
        if (chunk[2] != 1)
            for (j = 0; j < chunk[8]; j++)
            {
                offset = *(int*) (chunk + 0x10 + j * 4);
                elist[*entry_count].EID = from_u32(chunk + offset + 4);
                elist[*entry_count].chunk = i;
                elist[*entry_count].esize = -1;
                if (*(int *)(chunk + offset + 8) == ENTRY_TYPE_GOOL && *(int*)(chunk + offset + 0xC) > 3)
                {
                    int item1_offset = *(int *)(chunk + offset + 0x10);
                    gool_table[*(int*)(chunk + offset + item1_offset)] = elist[*entry_count].EID;
                }
                (*entry_count)++;
            }
    }
}


/** \brief
 *  For each valid file it adds it to the nsf(texture) or to the entry list.
 *  Collects relatives.
 *
 * \param df DIR*                       directory/folder
 * \param dirpath char*                 path to the directory/folder
 * \param chunks unsigned char**        array of 64kB chunks
 * \param elist ENTRY*                  entry list
 * \param chunk_border_texture int*     max index of a texture chunk
 * \param entry_count int*              entry count
 * \param spawns SPAWNS*                spawns, spawns get updated here
 * \param gool_table unsigned int*      table that contains GOOL entries on indexes that correspond to their types
 * \return void
 */
void build_read_folder(DIR *df, char *dirpath, unsigned char **chunks, ENTRY *elist, int *chunk_border_texture, int *entry_count, SPAWNS *spawns, unsigned int* gool_table) {
    struct dirent *de;
    char temp[500];
    FILE *file = NULL;
    int fsize;
    unsigned char entry[CHUNKSIZE];

    while ((de = readdir(df)) != NULL)
    if ((de->d_name)[0] != '.') {
        sprintf(temp, "%s\\%s", dirpath, de->d_name);
        if (file != NULL) {
            fclose(file);
            file = NULL;
        }
        if ((file = fopen(temp, "rb")) == NULL) continue;
        fseek(file, 0, SEEK_END);
        fsize = ftell(file);
        rewind(file);
        fread(entry, fsize, sizeof(unsigned char), file);
        if (fsize == CHUNKSIZE && from_u16(entry + 0x2) == CHUNK_TYPE_TEXTURE) {
            if (build_get_base_chunk_border(from_u32(entry + 4), chunks, *chunk_border_texture) > 0) continue;
            chunks[*chunk_border_texture] = (unsigned char *) calloc(CHUNKSIZE, sizeof(unsigned char));
            memcpy(chunks[*chunk_border_texture], entry, CHUNKSIZE);
            elist[*entry_count].EID = from_u32(entry + 4);
            elist[*entry_count].chunk = *chunk_border_texture;
            elist[*entry_count].data = NULL;
            elist[*entry_count].related = NULL;
            (*entry_count)++;
            (*chunk_border_texture)++;
            continue;
        }

        if (from_u32(entry) != MAGIC_ENTRY) continue;
        if (build_get_index(from_u32(entry + 4), elist, *entry_count) >= 0) continue;

        elist[*entry_count].EID = from_u32(entry + 4);
        elist[*entry_count].chunk = -1;
        elist[*entry_count].esize = fsize;

        if (entry[8] == ENTRY_TYPE_ZONE)
            build_check_item_count(entry, elist[*entry_count].EID);
        if (entry[8] == ENTRY_TYPE_ZONE)
            elist[*entry_count].related = build_get_zone_relatives(entry, spawns);
        if (entry[8] == ENTRY_TYPE_GOOL && from_u32(entry + 0xC) == 6)
            elist[*entry_count].related = build_get_gool_relatives(entry, fsize);

        elist[*entry_count].data = (unsigned char *) malloc(fsize * sizeof(unsigned char));
        memcpy(elist[*entry_count].data, entry, fsize);
        if (build_entry_type(elist[*entry_count]) == ENTRY_TYPE_GOOL && *(elist[*entry_count].data + 8) > 3) {
            int item1_offset = *(int *)(elist[*entry_count].data + 0x10);
            int gool_type = *(int*)(elist[*entry_count].data + item1_offset);
            char temp[100];
            if (gool_type > 63 || gool_type < 0) {
                printf("[warning] GOOL entry %s has invalid type specified in the third item (%2d)!\n", eid_conv(elist[*entry_count].EID, temp), gool_type);
                continue;
            }
            gool_table[gool_type] = elist[*entry_count].EID;
        }
        (*entry_count)++;
        qsort(elist, *entry_count, sizeof(ENTRY), cmp_entry_eid);
    }

    fclose(file);
}


/** \brief
 *  Reads the info from file the user has to provide, first part has permaloaded entries,
 *  second has a list of type/subtype dependencies
 *
 * \param permaloaded LIST*             list of permaloaded entries (created here)
 * \param subtype_info DEPENDENCIES*    list of type/subtype dependencies (created here)
 * \param file_path char*               path of the input file
 * \param elist ENTRY*                  entry list
 * \param entry_count int               entry count
 * \return int                          1 if all went good, 0 if something is wrong
 */
int build_read_entry_config(LIST *permaloaded, DEPENDENCIES *subtype_info, DEPENDENCIES *collisions, ENTRY *elist, int entry_count, unsigned int *gool_table) {
    int i, j, perma_count, subcount, valid = 1;
    char temp[6];

    char fpaths[FPATH_COUNT][MAX] = {0};      // paths to files, fpaths contains user-input metadata like perma list file
    build_ask_list_paths(fpaths);

    LIST perma = init_list();
    FILE *file = fopen(fpaths[0], "r");
    if (file == NULL) {
        printf("File with permaloaded entries could not be opened\n");
        return 0;
    }

    fscanf(file, "%d", &perma_count);
    for (i = 0; i < perma_count; i++) {
        fscanf(file, "%5s\n", temp);
        int index = build_get_index(eid_to_int(temp), elist, entry_count);
        if (index == -1) {
            printf("[ERROR] invalid permaloaded entry, won't proceed :\t%s\n", temp);
            valid = 0;
            continue;
        }

        list_insert(&perma, eid_to_int(temp));

        if (build_entry_type(elist[index]) == ENTRY_TYPE_ANIM) {
            unsigned int model = build_get_model(elist[index].data);
            list_insert(&perma, model);

            int model_index = build_get_index(model, elist, entry_count);
            if (model_index == -1) continue;

            build_add_model_textures_to_list(elist[model_index].data, &perma);
        }
    }

    fclose(file);
    file = fopen(fpaths[1], "r");
    if (file == NULL) {
        printf("File with type/subtype dependencies could not be opened\n");
        return 0;
    }
    fscanf(file, "%d", &subcount);

    DEPENDENCIES subinfo;
    subinfo.count = subcount;
    subinfo.array = (INF *) malloc(subcount * sizeof(INF));
    int type, subtype, counter;
    for (i = 0; i < subcount; i++) {
        fscanf(file, "%d, %d, %d", &type, &subtype, &counter);
        subinfo.array[i].type = type;
        subinfo.array[i].subtype = subtype;
        subinfo.array[i].dependencies = init_list();
        list_insert(&subinfo.array[i].dependencies, gool_table[type]);
        for (j = 0; j < counter; j++) {
            fscanf(file, ", %5s", temp);
            int index = build_get_index(eid_to_int(temp), elist, entry_count);
            if (index == -1) {
                printf("[warning] unknown entry reference in object dependency list, will be skipped:\t %s\n", temp);
                continue;
            }
            list_insert(&(subinfo.array[i].dependencies), eid_to_int(temp));

            if (build_entry_type(elist[index]) == ENTRY_TYPE_ANIM) {
                unsigned int model = build_get_model(elist[index].data);
                list_insert(&subinfo.array[i].dependencies, model);

                int model_index = build_get_index(model, elist, entry_count);
                if (model_index == -1) continue;

                build_add_model_textures_to_list(elist[model_index].data, &subinfo.array[i].dependencies);
            }
        }
    }

    fclose(file);
    file = fopen(fpaths[2], "r");
    if (file == NULL) {
        printf("File with collision dependencie (not input by user now, instead hardcoded to be searched for in current directory\n");
        return 0;
    }

    int coll_count;
    fscanf(file,"%d", &coll_count);
    DEPENDENCIES coll;
    coll.count = coll_count;
    coll.array = (INF *) malloc(coll_count * sizeof(INF));
    for (i = 0; i < coll_count; i++) {
        int code;
        fscanf(file, "%x, %d", &code, &counter);
        coll.array[i].type = code;
        coll.array[i].subtype = -1;
        coll.array[i].dependencies = init_list();
        for (j = 0; j < counter; j++) {
            fscanf(file, ", %5s", temp);
            int index = build_get_index(eid_to_int(temp), elist, entry_count);
            if (index == -1) {
                printf("[warning] unknown entry reference in collision dependency list, will be skipped: %s\n", temp);
                continue;
            }

            list_insert(&(coll.array[i].dependencies), eid_to_int(temp));

            if (build_entry_type(elist[index]) == ENTRY_TYPE_ANIM) {
                unsigned int model = build_get_model(elist[index].data);
                list_insert(&coll.array[i].dependencies, model);

                int model_index = build_get_index(model, elist, entry_count);
                if (model_index == -1) continue;

                build_add_model_textures_to_list(elist[model_index].data, &coll.array[i].dependencies);
            }
        }

    }

    *permaloaded = perma;
    *subtype_info = subinfo;
    *collisions = coll;
    fclose(file);
    if (!valid) {
        printf("Cannot proceed with invalid items, fix that\n");
        return 0;
    }
    return 1;
}


/** \brief
 *  Gets list of special entries in the zone's first item. For more info see function below.
 *
 * \param zone unsigned char*           zone to get the stuff from
 * \return LIST                         list of special entries
 */
LIST build_read_special_entries(unsigned char *zone) {
    LIST special_entries = init_list();
    int item1off = from_u32(zone + 0x10);
    unsigned char *metadata_ptr = zone + item1off + C2_SPECIAL_METADATA_OFFSET;
    int special_entry_count = from_u32(metadata_ptr);

    for (int i = 1; i <= special_entry_count; i++) {
        unsigned int entry = from_u32(metadata_ptr + i * 4);
        *(unsigned int *)(metadata_ptr + i * 4) = 0;
        list_insert(&special_entries, entry);
    }

    *(unsigned int *)metadata_ptr = 0;
    return special_entries;
}


/** \brief
 *  Adds entries specified in the zone's first item by the user. Usually entries that cannot be tied to a specific object or collision.
 *  Similar to the permaloaded and dependency list, it also checks whether the items are valid and in the case of animations adds their model
 *  and the model's texture to the list.
 *
 * \param full_load LIST*               non-delta load lists
 * \param cam_length int                length of the camera path and load list array
 * \param zone ENTRY                    zone to get the stuff from
 * \return void
 */
LIST build_get_special_entries(ENTRY zone, ENTRY *elist, int entry_count) {
    LIST special_entries = build_read_special_entries(zone.data);
    LIST iteration_clone = init_list();
    list_copy_in(&iteration_clone, special_entries);

    for (int i = 0; i < iteration_clone.count; i++) {
        char temp[100], temp2[100], temp3[100];
        int item = iteration_clone.eids[i];
        int index = build_get_index(item, elist, entry_count);
        if (index == -1) {
            printf("[error] Zone %s special entry list contains entry %s which is not present.\n", eid_conv(zone.EID, temp), eid_conv(item, temp2));
            list_remove(&special_entries, item);
            continue;
        }

        if (build_entry_type(elist[index]) == ENTRY_TYPE_ANIM) {
            unsigned int model = build_get_model(elist[index].data);
            int model_index = build_get_index(model, elist, entry_count);
            if (model_index == -1 || build_entry_type(elist[model_index]) != ENTRY_TYPE_MODEL) {
                printf("[error] Zone %s special entry list contains animation %s that uses model %s that is not present or is not a model\n",
                       eid_conv(zone.EID, temp), eid_conv(item, temp2), eid_conv(model, temp3));
                continue;
            }

            list_insert(&special_entries, model);
            build_add_model_textures_to_list(elist[model_index].data, &special_entries);
        }
    }

    return special_entries;
}


/** \brief
 *  Gets relatives of zones.
 *
 * \param entry unsigned char*          entry data
 * \param spawns SPAWNS*                during relative collection it searches for possible spawns
 * \return unsigned int*                array of relatives relatives[count + 1], relatives[0] contains count or NULL
 */
unsigned int* build_get_zone_relatives(unsigned char *entry, SPAWNS *spawns) {
    int entity_count, item1len, relcount, camcount, neighbourcount, scencount, i, entry_count = 0;
    unsigned int* relatives;

    int item1off = get_nth_item_offset(entry, 0);
    int item2off = get_nth_item_offset(entry, 1);
    item1len = item2off - item1off;
    if (!(item1len == 0x358 || item1len == 0x318)) return NULL;

    camcount = build_get_cam_item_count(entry);
    if (camcount == 0) return NULL;

    entity_count = build_get_entity_count(entry);
    scencount = build_get_scen_count(entry);
    neighbourcount = build_get_neighbour_count(entry);
    relcount = camcount/3 + neighbourcount + scencount + 1;

    if (relcount == 1) return NULL;

    if (from_u32(&entry[item1off + C2_MUSIC_REF + item1len - 0x318]) != EID_NONE)
        relcount++;
    relatives = (unsigned int *) malloc(relcount * sizeof(unsigned int));

    relatives[entry_count++] = relcount - 1;

    for (i = 0; i < (camcount/3); i++)
        relatives[entry_count++] = build_get_slst(entry + from_u32(entry + 0x18 + 0xC * i));

    LIST neighbours = build_get_neighbours(entry);
    for (i = 0; i < neighbours.count; i++)
        relatives[entry_count++] = neighbours.eids[i];

    for (i = 0; i < scencount; i++)
        relatives[entry_count++] = from_u32(entry + item1off + 0x4 + 0x30 * i);

    if (from_u32(&entry[item1off + C2_MUSIC_REF + item1len - 0x318]) != EID_NONE)
        relatives[entry_count++] = from_u32(&entry[item1off + C2_MUSIC_REF + item1len - 0x318]);

    for (i = 0; i < entity_count; i++) {
        int *coords_ptr;
        coords_ptr = build_seek_spawn(entry + from_u32(entry + 0x18 + 4 * camcount + 4 * i));
        if (coords_ptr != NULL) {
            spawns->spawn_count += 1;
            int cnt = spawns->spawn_count;
            spawns->spawns = (SPAWN *) realloc(spawns->spawns, cnt * sizeof(SPAWN));
            spawns->spawns[cnt - 1].x = coords_ptr[0] + *(int*)(entry + item2off);
            spawns->spawns[cnt - 1].y = coords_ptr[1] + *(int*)(entry + item2off + 4);
            spawns->spawns[cnt - 1].z = coords_ptr[2] + *(int*)(entry + item2off + 8);
            spawns->spawns[cnt - 1].zone = from_u32(entry + 0x4);
            free(coords_ptr);
        }
    }
    return relatives;
}


/** \brief
 *  Gets gool relatives excluding models.
 *
 * \param entry unsigned char*          entry data
 * \param entrysize int                 entry size
 * \return unsigned int*                array of relatives relatives[count + 1], relatives[0] == count
 */
unsigned int* build_get_gool_relatives(unsigned char *entry, int entrysize) {
    int curr_off, type = 0, help;
    int i, counter = 0;
    char temp[6];
    int curr_eid;
    unsigned int local[256];
    unsigned int *relatives = NULL;

    curr_off = get_nth_item_offset(entry, 5);

    while (curr_off < entrysize)
        switch (entry[curr_off]) {
            case 1:
                if (type == 0) {
                    help = from_u32(entry + curr_off + 0xC) & 0xFF00FFFF;
                    if (help <= 05 && help > 0) type = 2;
                        else type = 3;
                }

                if (type == 2) {
                    curr_eid = from_u32(entry + curr_off + 4);
                    eid_conv(curr_eid, temp);
                    if (temp[4] == 'G' || temp[4] == 'V')
                        local[counter++] = curr_eid;
                    curr_off += 0xC;
                }
                else {
                    for (i = 0; i < 4; i++) {
                        curr_eid = from_u32(entry + curr_off + 4 + 4*i);
                        eid_conv(curr_eid, temp);
                        if (temp[4] == 'G' || temp[4] == 'V')
                            local[counter++] = curr_eid;
                    }
                    curr_off += 0x14;
                }
                break;
            case 2:
                curr_off += 0x8 + 0x10 * entry[curr_off + 2];
                break;
            case 3:
                curr_off += 0x8 + 0x14 * entry[curr_off + 2];
                break;
            case 4:
                curr_off = entrysize;
                break;
            case 5:
                curr_off += 0xC + 0x18 * entry[curr_off + 2] * entry[curr_off + 8];
                break;
        }


    if (!counter) return NULL;

    relatives = (unsigned int *) malloc((counter + 1) * sizeof(unsigned int));
    relatives[0] = counter;
    for (i = 0; i < counter; i++) relatives[i + 1] = local[i];

    return relatives;
}

/** \brief
 *  Searches the entity, if it has (correct) type and subtype and coords property,
 *  returns them as int[3]. Usually set to accept willy and checkpoint entities.
 *
 * \param item unsigned char*           entity data
 * \return int*                         int[3] with xyz coords the entity if it meets criteria or NULL
 */
int* build_seek_spawn(unsigned char *item) {
    int i, code, offset, type = -1, subtype = -1, coords_offset = -1;
    for (i = 0; (unsigned) i < from_u32(item + 0xC); i++) {
        code = from_u16(item + 0x10 + 8*i);
        offset = from_u16(item + 0x12 + 8*i) + OFFSET;
        if (code == ENTITY_PROP_TYPE)
            type = from_u32(item + offset + 4);
        if (code == ENTITY_PROP_SUBTYPE)
            subtype = from_u32(item + offset + 4);
        if (code == ENTITY_PROP_PATH)
            coords_offset = offset;
    }

    if ((!type && !subtype && coords_offset != -1) || (type == 34 && subtype == 4 && coords_offset != -1)) {
        int *coords = (int*) malloc(3 * sizeof(int));
        for (i = 0; i < 3; i++)
            coords[i] = (*(short int*)(item + coords_offset + 4 + 2 * i)) * 4;
        return coords;
    }

    return NULL;
}


/** \brief
 *  For each gool entry it searches related animatons and adds the model entries
 *  referenced by the animations to the gool entry's relatives.
 *
 * \param elist ENTRY*                  current entry list
 * \param entry_count int               current amount of entries
 * \return void
 */
void build_get_model_references(ENTRY *elist, int entry_count) {
    int i, j;
    unsigned int new_relatives[250];
    for (i = 0; i < entry_count; i++) {
        if ((elist[i].related != NULL) && (from_u32(elist[i].data + 8) == ENTRY_TYPE_GOOL)) {
            int relative_index;
            int new_counter = 0;
            for (j = 0; (unsigned) j < elist[i].related[0]; j++)
                if ((relative_index = build_get_index(elist[i].related[j + 1], elist, entry_count)) >= 0)
                    if (elist[relative_index].data != NULL && (from_u32(elist[relative_index].data + 8) == 1))
                        new_relatives[new_counter++] = build_get_model(elist[relative_index].data);

            if (new_counter) {
                int relative_count;
                relative_count = elist[i].related[0];
                elist[i].related = (unsigned int *) realloc(elist[i].related, (relative_count + new_counter + 1) * sizeof(unsigned int));
                for (j = 0; j < new_counter; j++)
                    elist[i].related[j + relative_count + 1] = new_relatives[j];
                elist[i].related[0] += new_counter;
            }
        }
    }
}


/** \brief
 *  For each entry zone's cam path it calculates its distance from the selected spawn (in cam paths) according
 *  to level's path links.
 *  Heuristic for determining whether a path is before or after another path relative to current spawn.
 *
 * \param elist ENTRY*                  list of entries
 * \param entry_count int               entry count
 * \param spawns SPAWNS                 struct with found/potential spawns
 * \return void
 */
void build_get_distance_graph(ENTRY *elist, int entry_count, SPAWNS spawns) {
    for (int i = 0; i < entry_count; i++) {
        if (build_entry_type(elist[i]) == ENTRY_TYPE_ZONE) {
            int cam_count = build_get_cam_item_count(elist[i].data)/3;
            elist[i].distances = (unsigned int *) malloc(cam_count * sizeof(unsigned int));
            elist[i].visited   = (unsigned int *) malloc(cam_count * sizeof(unsigned int));

            for (int j = 0; j < cam_count; j++) {
                elist[i].distances[j] = INT_MAX;
                elist[i].visited[j] = 0;
            }
        }
        else {
            elist[i].distances = NULL;
            elist[i].visited = NULL;
        }
    }

    QUEUE graph = graph_init();
    int start_index = build_get_index(spawns.spawns[0].zone, elist, entry_count);
    graph_add(&graph, elist, start_index, 0);

    while (1) {
        int top_zone;
        int top_cam;
        graph_pop(&graph, &top_zone, &top_cam);
        if (top_zone == -1 && top_cam == -1)
            break;

        LIST links = build_get_links(elist[top_zone].data, 2 + 3 * top_cam);
        for (int i = 0; i < links.count; i++)
        {
            LINK link = int_to_link(links.eids[i]);
            int neighbour_count = build_get_neighbour_count(elist[top_zone].data);
            unsigned int neighbours[neighbour_count];
            int item1off = from_u32(elist[top_zone].data + 0x10);
            for (int j = 0; j < neighbour_count; j++)
                neighbours[j] = from_u32(elist[top_zone].data + item1off + C2_NEIGHBOURS_START + 4 + 4 * j);
            int neighbour_index = build_get_index(neighbours[link.zone_index], elist, entry_count);

            if (elist[neighbour_index].visited[link.cam_index] == 0)
                graph_add(&graph, elist, neighbour_index, link.cam_index);
        }
    }


    /*for (int i = 0; i < entry_count; i++)
        if (build_entry_type(elist[i]) == ENTRY_TYPE_ZONE)
        {
            int cam_count = build_get_cam_count(elist[i].data)/3;
            char temp[100];
            for (int j = 0; j < cam_count; j++)
                printf("Zone %s campath %d has distance %d\n", eid_conv(elist[i].EID, temp), j, elist[i].distances[j]);
        }*/
}


/** \brief
 *  Removes references to entries that are only in the base level.
 *
 * \param elist ENTRY*                  list of entries
 * \param entry_count int               current amount of entries
 * \param entry_count_base int
 * \return void
 */
void build_remove_invalid_references(ENTRY *elist, int entry_count, int entry_count_base) {
    int i, j, k;
    unsigned int new_relatives[250];

    for (i = 0; i < entry_count; i++) {
        if (elist[i].related == NULL) continue;
        for (j = 1; (unsigned) j < elist[i].related[0] + 1; j++) {
            int relative_index;
            relative_index = build_get_index(elist[i].related[j], elist, entry_count);
            if (relative_index < entry_count_base) elist[i].related[j] = 0;
            if (elist[i].related[j] == elist[i].EID) elist[i].related[j] = 0;

            for (k = j + 1; (unsigned) k < elist[i].related[0] + 1; k++)
                if (elist[i].related[j] == elist[i].related[k])
                    elist[i].related[k] = 0;
        }

        int counter = 0;
        for (j = 1; (unsigned) j < elist[i].related[0] + 1; j++)
            if (elist[i].related[j] != 0) new_relatives[counter++] = elist[i].related[j];

        if (counter == 0) {
            elist[i].related = (unsigned int *) realloc(elist[i].related, 0);
            continue;
        }

        elist[i].related = (unsigned int *) realloc(elist[i].related, (counter + 1) * sizeof(unsigned int));
        elist[i].related[0] = counter;
        for (j = 0; j < counter; j++)
            elist[i].related[j + 1] = new_relatives[j];
    }
}

int build_read_and_parse_build(int *level_ID, FILE **nsfnew, FILE **nsd, int* chunk_border_texture, unsigned int* gool_table,
                                ENTRY *elist, int* entry_count, unsigned char **chunks, SPAWNS *spawns) {
    char dirpath[MAX], nsfpath[MAX], lcltemp[MAX + 20]; // + 20 to get rid of a warning
    printf("Input the path to the base level (.nsf)[CAN BE A BLANK FILE]:\n");
    scanf(" %[^\n]", nsfpath);
    path_fix(nsfpath);

    printf("\nInput the path to the folder whose contents you want to import:\n");
    scanf(" %[^\n]", dirpath);
    path_fix(dirpath);

    FILE *nsf = NULL;
    if ((nsf = fopen(nsfpath,"rb")) == NULL) {
        printf("[ERROR] Could not open selected NSF\n");
        return 1;
    }

    DIR *df = NULL;
    if ((df = opendir(dirpath)) == NULL) {
        printf("[ERROR] Could not open selected directory\n");
        fclose(nsf);
        return 1;
    }

    *level_ID = build_ask_ID();

    *(strrchr(nsfpath,'\\') + 1) = '\0';
    sprintf(lcltemp,"%s\\S00000%02X.NSF", nsfpath, *level_ID);
    *nsfnew = fopen(lcltemp, "wb");
    *(strchr(lcltemp, '\0') - 1) = 'D';
    *nsd = fopen(lcltemp, "wb");

    //chunk_border_base = build_get_chunk_count_base(nsf);
    //build_read_nsf(elist, chunk_border_base, chunks, chunk_border_texture, &entry_count, nsf, gool_table);    // rn base level is not considered
    build_read_folder(df, dirpath, chunks, elist, chunk_border_texture, entry_count, spawns, gool_table);
    fclose(nsf);
    return 0;
}

int build_read_and_parse_rebuild(int *level_ID, FILE **nsfnew, FILE **nsd, int* chunk_border_texture, unsigned int* gool_table,
                                  ENTRY *elist, int *entry_count, unsigned char **chunks, SPAWNS* spawns) {
    char nsfpath[MAX], lcltemp[MAX + 20]; // + 20 to get rid of a warning
    printf("Input the path to the level (.nsf) you want to rebuild:\n");
    scanf(" %[^\n]", nsfpath);
    path_fix(nsfpath);

    FILE *nsf = NULL;
    if ((nsf = fopen(nsfpath, "rb")) == NULL)  {
        printf("[ERROR] Could not open selected NSF\n");
        return 1;
    }

    *level_ID = build_ask_ID();

    *(strrchr(nsfpath,'\\') + 1) = '\0';
    sprintf(lcltemp,"%s\\S00000%02X.NSF", nsfpath, *level_ID);
    *nsfnew = fopen(lcltemp, "wb");
    *(strchr(lcltemp, '\0') - 1) = 'D';
    *nsd = fopen(lcltemp, "wb");

    int nsf_chunk_count = build_get_chunk_count_base(nsf);

    unsigned char buffer[CHUNKSIZE];
    for (int i = 0; i < nsf_chunk_count; i++) {
        fread(buffer, sizeof(unsigned char), CHUNKSIZE, nsf);
        int chunk_entry_count = from_u32(buffer + 0x8);
        if (from_u16(buffer + 0x2) == CHUNK_TYPE_TEXTURE) {
            chunks[*chunk_border_texture] = (unsigned char *) calloc(CHUNKSIZE, sizeof(unsigned char));
            memcpy(chunks[*chunk_border_texture], buffer, CHUNKSIZE);
            elist[*entry_count].EID = from_u32(buffer + 4);
            elist[*entry_count].chunk = *chunk_border_texture;
            elist[*entry_count].data = NULL;
            elist[*entry_count].related = NULL;
            *entry_count += 1;
            *chunk_border_texture += 1;
        }
        else
        for (int j = 0; j < chunk_entry_count; j++) {
            int start_offset = get_nth_item_offset(buffer, j);
            int end_offset = get_nth_item_offset(buffer, j + 1);
            int entry_size = end_offset - start_offset;

            elist[*entry_count].chunk = -1;
            elist[*entry_count].EID = from_u32(buffer + start_offset + 0x4);
            elist[*entry_count].esize = entry_size;
            elist[*entry_count].related = NULL;
            elist[*entry_count].data = (unsigned char *) malloc(entry_size);
            memcpy(elist[*entry_count].data, buffer + start_offset, entry_size);

            if (build_entry_type(elist[*entry_count]) == ENTRY_TYPE_ZONE)
                build_check_item_count(elist[*entry_count].data, elist[*entry_count].EID);
            if (build_entry_type(elist[*entry_count]) == ENTRY_TYPE_ZONE)
                elist[*entry_count].related = build_get_zone_relatives(elist[*entry_count].data, spawns);
            if (build_entry_type(elist[*entry_count]) == ENTRY_TYPE_GOOL && from_u32(elist[*entry_count].data + 0xC) == 6)
                elist[*entry_count].related = build_get_gool_relatives(elist[*entry_count].data, entry_size);

            if (build_entry_type(elist[*entry_count]) == ENTRY_TYPE_GOOL && *(elist[*entry_count].data + 8) > 3) {
                int item1_offset = *(int *)(elist[*entry_count].data + 0x10);
                int gool_type = *(int*)(elist[*entry_count].data + item1_offset);
                char temp[100];
                if (gool_type > 63 || gool_type < 0) {
                    printf("[warning] GOOL entry %s has invalid type specified in the third item (%2d)!\n", eid_conv(elist[*entry_count].EID, temp), gool_type);
                    continue;
                }
                gool_table[gool_type] = elist[*entry_count].EID;
            }

            *entry_count += 1;
            qsort(elist, *entry_count, sizeof(ENTRY), cmp_entry_eid);
        }
    }
    fclose(nsf);
    return 0;
}
